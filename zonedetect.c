/*
 * Copyright (c) 2018, Bertold Van den Bergh (vandenbergh@bertold.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the author nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR DISTRIBUTOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#if defined(_MSC_VER) || defined(__MINGW32__)
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(_POSIX_VERSION)
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "zonedetect.h"

enum ZDInternalError {
    ZD_OK,
    ZD_E_DB_OPEN,
    ZD_E_DB_SEEK,
    ZD_E_DB_MMAP,
#if defined(_MSC_VER) || defined(__MINGW32__)
    ZD_E_DB_MMAP_MSVIEW,
    ZD_E_DB_MAP_EXCEPTION,
    ZD_E_DB_MUNMAP_MSVIEW,
#endif
    ZD_E_DB_MUNMAP,
    ZD_E_DB_CLOSE,
    ZD_E_PARSE_HEADER
};

struct ZoneDetectOpaque {
#if defined(_MSC_VER) || defined(__MINGW32__)
    HANDLE fd;
    HANDLE fdMap;
    int32_t length;
    int32_t padding;
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(_POSIX_VERSION)
    int fd;
    off_t length;
#else
    int length;
#endif

    uint8_t closeType;
    uint8_t *mapping;

    uint8_t tableType;
    uint8_t version;
    uint8_t precision;
    uint8_t numFields;

    char *notice;
    char **fieldNames;

    uint32_t bboxOffset;
    uint32_t metadataOffset;
    uint32_t dataOffset;
};

static void (*zdErrorHandler)(int, int);
static void zdError(enum ZDInternalError errZD, int errNative)
{
    if (zdErrorHandler) zdErrorHandler((int)errZD, errNative);
}

static int32_t ZDFloatToFixedPoint(float input, float scale, unsigned int precision)
{
    const float inputScaled = input / scale;
    return (int32_t)(inputScaled * (float)(1 << (precision - 1)));
}

static float ZDFixedPointToFloat(int32_t input, float scale, unsigned int precision)
{
    const float value = (float)input / (float)(1 << (precision - 1));
    return value * scale;
}

static unsigned int ZDDecodeVariableLengthUnsigned(const ZoneDetect *library, uint32_t *index, uint64_t *result)
{
    if(*index >= (uint32_t)library->length) {
        return 0;
    }

    uint64_t value = 0;
    unsigned int i = 0;
#if defined(_MSC_VER)
    __try {
#endif
        uint8_t *const buffer = library->mapping + *index;
        uint8_t *const bufferEnd = library->mapping + library->length - 1;

        unsigned int shift = 0;
        while(1) {
            value |= ((((uint64_t)buffer[i]) & UINT8_C(0x7F)) << shift);
            shift += 7u;

            if(!(buffer[i] & UINT8_C(0x80))) {
                break;
            }

            i++;
            if(buffer + i > bufferEnd) {
                return 0;
            }
        }
#if defined(_MSC_VER)
    } __except(GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH) { /* file mapping SEH exception occurred */
        zdError(ZD_E_DB_MAP_EXCEPTION, (int)GetLastError());
        return 0;
    }
#endif

    i++;
    *result = value;
    *index += i;
    return i;
}

static unsigned int ZDDecodeVariableLengthUnsignedReverse(const ZoneDetect *library, uint32_t *index, uint64_t *result)
{
    uint32_t i = *index;

    if(*index >= (uint32_t)library->length) {
        return 0;
    }

#if defined(_MSC_VER)
    __try {
#endif

        if(library->mapping[i] & UINT8_C(0x80)) {
            return 0;
        }

        if(!i) {
            return 0;
        }
        i--;

        while(library->mapping[i] & UINT8_C(0x80)) {
            if(!i) {
                return 0;
            }
            i--;
        }

#if defined(_MSC_VER)
    } __except(GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH) { /* file mapping SEH exception occurred */
        zdError(ZD_E_DB_MAP_EXCEPTION, (int)GetLastError());
        return 0;
    }
#endif

    *index = i;

    i++;

    uint32_t i2 = i;
    return ZDDecodeVariableLengthUnsigned(library, &i2, result);
}

static int64_t ZDDecodeUnsignedToSigned(uint64_t value)
{
    return (value & 1) ? -(int64_t)(value / 2) : (int64_t)(value / 2);
}

static unsigned int ZDDecodeVariableLengthSigned(const ZoneDetect *library, uint32_t *index, int32_t *result)
{
    uint64_t value = 0;
    const unsigned int retVal = ZDDecodeVariableLengthUnsigned(library, index, &value);
    *result = (int32_t)ZDDecodeUnsignedToSigned(value);
    return retVal;
}

static char *ZDParseString(const ZoneDetect *library, uint32_t *index)
{
    uint64_t strLength;
    if(!ZDDecodeVariableLengthUnsigned(library, index, &strLength)) {
        return NULL;
    }

    uint32_t strOffset = *index;
    unsigned int remoteStr = 0;
    if(strLength >= 256) {
        strOffset = library->metadataOffset + (uint32_t)strLength - 256;
        remoteStr = 1;

        if(!ZDDecodeVariableLengthUnsigned(library, &strOffset, &strLength)) {
            return NULL;
        }

        if(strLength > 256) {
            return NULL;
        }
    }

    char *const str = malloc((size_t)(strLength + 1));

    if(str) {
#if defined(_MSC_VER)
        __try {
#endif
            size_t i;
            for(i = 0; i < strLength; i++) {
                str[i] = (char)(library->mapping[strOffset + i] ^ UINT8_C(0x80));
            }
#if defined(_MSC_VER)
        } __except(GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
                   ? EXCEPTION_EXECUTE_HANDLER
                   : EXCEPTION_CONTINUE_SEARCH) { /* file mapping SEH exception occurred */
            zdError(ZD_E_DB_MAP_EXCEPTION, (int)GetLastError());
            return 0;
        }
#endif
        str[strLength] = 0;
    }

    if(!remoteStr) {
        *index += (uint32_t)strLength;
    }

    return str;
}

static int ZDParseHeader(ZoneDetect *library)
{
    if(library->length < 7) {
        return -1;
    }

#if defined(_MSC_VER)
    __try {
#endif
        if(memcmp(library->mapping, "PLB", 3)) {
            return -1;
        }

        library->tableType = library->mapping[3];
        library->version   = library->mapping[4];
        library->precision = library->mapping[5];
        library->numFields = library->mapping[6];
#if defined(_MSC_VER)
    } __except(GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH) { /* file mapping SEH exception occurred */
        zdError(ZD_E_DB_MAP_EXCEPTION, (int)GetLastError());
        return 0;
    }
#endif

    if(library->version >= 2) {
        return -1;
    }

    uint32_t index = UINT32_C(7);

    library->fieldNames = malloc(library->numFields * sizeof *library->fieldNames);
    if (!library->fieldNames) {
        return -1;
    }

    size_t i;
    for(i = 0; i < library->numFields; i++) {
        library->fieldNames[i] = ZDParseString(library, &index);
    }

    library->notice = ZDParseString(library, &index);
    if(!library->notice) {
        return -1;
    }

    uint64_t tmp;
    /* Read section sizes */
    /* By memset: library->bboxOffset = 0 */

    if(!ZDDecodeVariableLengthUnsigned(library, &index, &tmp)) return -1;
    library->metadataOffset = (uint32_t)tmp + library->bboxOffset;

    if(!ZDDecodeVariableLengthUnsigned(library, &index, &tmp))return -1;
    library->dataOffset = (uint32_t)tmp + library->metadataOffset;

    if(!ZDDecodeVariableLengthUnsigned(library, &index, &tmp)) return -1;

    /* Add header size to everything */
    library->bboxOffset += index;
    library->metadataOffset += index;
    library->dataOffset += index;

    /* Verify file length */
    if(tmp + library->dataOffset != (uint32_t)library->length) {
        return -2;
    }

    return 0;
}

static int ZDPointInBox(int32_t xl, int32_t x, int32_t xr, int32_t yl, int32_t y, int32_t yr)
{
    if((xl <= x && x <= xr) || (xr <= x && x <= xl)) {
        if((yl <= y && y <= yr) || (yr <= y && y <= yl)) {
            return 1;
        }
    }

    return 0;
}

static uint32_t ZDUnshuffle(uint64_t w)
{
    w &=                  0x5555555555555555llu;
    w = (w | (w >> 1))  & 0x3333333333333333llu;
    w = (w | (w >> 2))  & 0x0F0F0F0F0F0F0F0Fllu;
    w = (w | (w >> 4))  & 0x00FF00FF00FF00FFllu;
    w = (w | (w >> 8))  & 0x0000FFFF0000FFFFllu;
    w = (w | (w >> 16)) & 0x00000000FFFFFFFFllu;
    return (uint32_t)w;
}

static void ZDDecodePoint(uint64_t point, int32_t* lat, int32_t* lon)
{
    *lat = (int32_t)ZDDecodeUnsignedToSigned(ZDUnshuffle(point));
    *lon = (int32_t)ZDDecodeUnsignedToSigned(ZDUnshuffle(point >> 1));
}

struct Reader {
    const ZoneDetect *library;
    uint32_t polygonIndex;

    uint64_t numVertices;

    uint8_t done, first;
    uint32_t referenceStart, referenceEnd;
    int32_t referenceDirection;

    int32_t pointLat, pointLon;
    int32_t firstLat, firstLon;
};

static void ZDReaderInit(struct Reader *reader, const ZoneDetect *library, uint32_t polygonIndex)
{
    memset(reader, 0, sizeof(*reader));

    reader->library = library;
    reader->polygonIndex = polygonIndex;

    reader->first = 1;
}

static int ZDReaderGetPoint(struct Reader *reader, int32_t *pointLat, int32_t *pointLon)
{
    int32_t diffLat = 0, diffLon = 0;

readNewPoint:
    if(reader->done > 1) {
        return 0;
    }

    if(reader->first && reader->library->version == 0) {
        if(!ZDDecodeVariableLengthUnsigned(reader->library, &reader->polygonIndex, &reader->numVertices)) return -1;
        if(!reader->numVertices) return -1;
    }

    uint8_t referenceDone = 0;

    if(reader->library->version == 1) {
        uint64_t point = 0;

        if(!reader->referenceDirection) {
            if(!ZDDecodeVariableLengthUnsigned(reader->library, &reader->polygonIndex, &point)) return -1;
        } else {
            if(reader->referenceDirection > 0) {
                /* Read reference forward */
                if(!ZDDecodeVariableLengthUnsigned(reader->library, &reader->referenceStart, &point)) return -1;
                if(reader->referenceStart >= reader->referenceEnd) {
                    referenceDone = 1;
                }
            } else if(reader->referenceDirection < 0) {
                /* Read reference backwards */
                if(!ZDDecodeVariableLengthUnsignedReverse(reader->library, &reader->referenceStart, &point)) return -1;
                if(reader->referenceStart <= reader->referenceEnd) {
                    referenceDone = 1;
                }
            }
        }

        if(!point) {
            /* This is a special marker, it is not allowed in reference mode */
            if(reader->referenceDirection) {
                return -1;
            }

            uint64_t value;
            if(!ZDDecodeVariableLengthUnsigned(reader->library, &reader->polygonIndex, &value)) return -1;

            if(value == 0) {
                reader->done = 2;
            } else if(value == 1) {
                int32_t diff;
                int64_t start;
                if(!ZDDecodeVariableLengthUnsigned(reader->library, &reader->polygonIndex, (uint64_t*)&start)) return -1;
                if(!ZDDecodeVariableLengthSigned(reader->library, &reader->polygonIndex, &diff)) return -1;

                reader->referenceStart = reader->library->dataOffset+(uint32_t)start;
                reader->referenceEnd = reader->library->dataOffset+(uint32_t)(start + diff);
                reader->referenceDirection = diff;
                if(diff < 0) {
                    reader->referenceStart--;
                    reader->referenceEnd--;
                }
                goto readNewPoint;
            }
        } else {
            ZDDecodePoint(point, &diffLat, &diffLon);
            if(reader->referenceDirection < 0) {
                diffLat = -diffLat;
                diffLon = -diffLon;
            }
        }
    }

    if(reader->library->version == 0) {
        if(!ZDDecodeVariableLengthSigned(reader->library, &reader->polygonIndex, &diffLat)) return -1;
        if(!ZDDecodeVariableLengthSigned(reader->library, &reader->polygonIndex, &diffLon)) return -1;
    }

    if(!reader->done) {
        reader->pointLat += diffLat;
        reader->pointLon += diffLon;
        if(reader->first) {
            reader->firstLat = reader->pointLat;
            reader->firstLon = reader->pointLon;
        }
    } else {
        /* Close the polygon (the closing point is not encoded) */
        reader->pointLat = reader->firstLat;
        reader->pointLon = reader->firstLon;
        reader->done = 2;
    }

    reader->first = 0;

    if(reader->library->version == 0) {
        reader->numVertices--;
        if(!reader->numVertices) {
            reader->done = 1;
        }
        if(!diffLat && !diffLon) {
            goto readNewPoint;
        }
    }

    if(referenceDone) {
        reader->referenceDirection = 0;
    }

    if(pointLat) {
        *pointLat = reader->pointLat;
    }

    if(pointLon) {
        *pointLon = reader->pointLon;
    }

    return 1;
}

static int ZDFindPolygon(const ZoneDetect *library, uint32_t wantedId, uint32_t* metadataIndexPtr, uint32_t* polygonIndexPtr)
{
    uint32_t polygonId = 0;
    uint32_t bboxIndex = library->bboxOffset;

    uint32_t metadataIndex = 0, polygonIndex = 0;

    while(bboxIndex < library->metadataOffset) {
        uint64_t polygonIndexDelta;
        int32_t metadataIndexDelta;
        int32_t tmp;
        if(!ZDDecodeVariableLengthSigned(library, &bboxIndex, &tmp)) break;
        if(!ZDDecodeVariableLengthSigned(library, &bboxIndex, &tmp)) break;
        if(!ZDDecodeVariableLengthSigned(library, &bboxIndex, &tmp)) break;
        if(!ZDDecodeVariableLengthSigned(library, &bboxIndex, &tmp)) break;
        if(!ZDDecodeVariableLengthSigned(library, &bboxIndex, &metadataIndexDelta)) break;
        if(!ZDDecodeVariableLengthUnsigned(library, &bboxIndex, &polygonIndexDelta)) break;

        metadataIndex += (uint32_t)metadataIndexDelta;
        polygonIndex += (uint32_t)polygonIndexDelta;

        if(polygonId == wantedId) {
            if(metadataIndexPtr) {
                metadataIndex += library->metadataOffset;
                *metadataIndexPtr = metadataIndex;
            }
            if(polygonIndexPtr) {
                polygonIndex += library->dataOffset;
                *polygonIndexPtr = polygonIndex;
            }
            return 1;
        }

        polygonId ++;
    }

    return 0;
}

static int32_t* ZDPolygonToListInternal(const ZoneDetect *library, uint32_t polygonIndex, size_t* length)
{
    struct Reader reader;
    ZDReaderInit(&reader, library, polygonIndex);

    size_t listLength = 2 * 100;
    size_t listIndex = 0;

    int32_t* list = malloc(sizeof(int32_t) * listLength);
    if(!list) {
        goto fail;
    }

    while(1) {
        int32_t pointLat, pointLon;
        int result = ZDReaderGetPoint(&reader, &pointLat, &pointLon);
        if(result < 0) {
            goto fail;
        } else if(result == 0) {
            break;
        }

        if(listIndex >= listLength) {
            listLength *= 2;
            if(listLength >= 1048576) {
                goto fail;
            }

            list = realloc(list, sizeof(int32_t) * listLength);
            if(!list) {
                goto fail;
            }
        }

        list[listIndex++] = pointLat;
        list[listIndex++] = pointLon;
    }

    if(length) {
        *length = listIndex;
    }

    return list;

fail:
    if(list) {
        free(list);
    }
    return NULL;
}

float* ZDPolygonToList(const ZoneDetect *library, uint32_t polygonId, size_t* lengthPtr)
{
    uint32_t polygonIndex;
    int32_t* data = NULL;
    float* flData = NULL;

    if(!ZDFindPolygon(library, polygonId, NULL, &polygonIndex)) {
        goto fail;
    }

    size_t length = 0;
    data = ZDPolygonToListInternal(library, polygonIndex, &length);

    if(!data) {
        goto fail;
    }

    flData = malloc(sizeof(float) * length);
    if(!flData) {
        goto fail;
    }

    size_t i;
    for(i = 0; i<length; i+= 2) {
        int32_t lat = data[i];
        int32_t lon = data[i+1];

        flData[i] = ZDFixedPointToFloat(lat, 90, library->precision);
        flData[i+1] = ZDFixedPointToFloat(lon, 180, library->precision);
    }

    if(lengthPtr) {
        *lengthPtr = length;
    }

    return flData;

fail:
    if(data) {
        free(data);
    }
    if(flData) {
        free(flData);
    }
    return NULL;
}

static ZDLookupResult ZDPointInPolygon(const ZoneDetect *library, uint32_t polygonIndex, int32_t latFixedPoint, int32_t lonFixedPoint, uint64_t *distanceSqrMin)
{
    int32_t pointLat, pointLon, prevLat = 0, prevLon = 0;
    int prevQuadrant = 0, winding = 0;

    uint8_t first = 1;

    struct Reader reader;
    ZDReaderInit(&reader, library, polygonIndex);

    while(1) {
        int result = ZDReaderGetPoint(&reader, &pointLat, &pointLon);
        if(result < 0) {
            return ZD_LOOKUP_PARSE_ERROR;
        } else if(result == 0) {
            break;
        }

        /* Check if point is ON the border */
        if(pointLat == latFixedPoint && pointLon == lonFixedPoint) {
            if(distanceSqrMin) *distanceSqrMin = 0;
            return ZD_LOOKUP_ON_BORDER_VERTEX;
        }

        /* Find quadrant */
        int quadrant;
        if(pointLat >= latFixedPoint) {
            if(pointLon >= lonFixedPoint) {
                quadrant = 0;
            } else {
                quadrant = 1;
            }
        } else {
            if(pointLon >= lonFixedPoint) {
                quadrant = 3;
            } else {
                quadrant = 2;
            }
        }

        if(!first) {
            int windingNeedCompare = 0, lineIsStraight = 0;
            float a = 0, b = 0;

            /* Calculate winding number */
            if(quadrant == prevQuadrant) {
                /* Do nothing */
            } else if(quadrant == (prevQuadrant + 1) % 4) {
                winding ++;
            } else if((quadrant + 1) % 4 == prevQuadrant) {
                winding --;
            } else {
                windingNeedCompare = 1;
            }

            /* Avoid horizontal and vertical lines */
            if((pointLon == prevLon || pointLat == prevLat)) {
                lineIsStraight = 1;
            }

            /* Calculate the parameters of y=ax+b if needed */
            if(!lineIsStraight && (distanceSqrMin || windingNeedCompare)) {
                a = ((float)pointLat - (float)prevLat) / ((float)pointLon - (float)prevLon);
                b = (float)pointLat - a * (float)pointLon;
            }

            int onStraight = ZDPointInBox(pointLat, latFixedPoint, prevLat, pointLon, lonFixedPoint, prevLon);
            if(lineIsStraight && (onStraight || windingNeedCompare)) {
                if(distanceSqrMin) *distanceSqrMin = 0;
                return ZD_LOOKUP_ON_BORDER_SEGMENT;
            }

            /* Jumped two quadrants. */
            if(windingNeedCompare) {
                /* Check if the target is on the border */
                const int32_t intersectLon = (int32_t)(((float)latFixedPoint - b) / a);
                if(intersectLon >= lonFixedPoint-1 && intersectLon <= lonFixedPoint+1) {
                    if(distanceSqrMin) *distanceSqrMin = 0;
                    return ZD_LOOKUP_ON_BORDER_SEGMENT;
                }

                /* Ok, it's not. In which direction did we go round the target? */
                const int sign = (intersectLon < lonFixedPoint) ? 2 : -2;
                if(quadrant == 2 || quadrant == 3) {
                    winding += sign;
                } else {
                    winding -= sign;
                }
            }

            /* Calculate closest point on line (if needed) */
            if(distanceSqrMin) {
                float closestLon, closestLat;
                if(!lineIsStraight) {
                    closestLon = ((float)lonFixedPoint + a * (float)latFixedPoint - a * b) / (a * a + 1);
                    closestLat = (a * ((float)lonFixedPoint + a * (float)latFixedPoint) + b) / (a * a + 1);
                } else {
                    if(pointLon == prevLon) {
                        closestLon = (float)pointLon;
                        closestLat = (float)latFixedPoint;
                    } else {
                        closestLon = (float)lonFixedPoint;
                        closestLat = (float)pointLat;
                    }
                }

                const int closestInBox = ZDPointInBox(pointLon, (int32_t)closestLon, prevLon, pointLat, (int32_t)closestLat, prevLat);

                int64_t diffLat, diffLon;
                if(closestInBox) {
                    /* Calculate squared distance to segment. */
                    diffLat = (int64_t)(closestLat - (float)latFixedPoint);
                    diffLon = (int64_t)(closestLon - (float)lonFixedPoint);
                } else {
                    /*
                     * Calculate squared distance to vertices
                     * It is enough to check the current point since the polygon is closed.
                     */
                    diffLat = (int64_t)(pointLat - latFixedPoint);
                    diffLon = (int64_t)(pointLon - lonFixedPoint);
                }

                /* Note: lon has half scale */
                uint64_t distanceSqr = (uint64_t)(diffLat * diffLat) + (uint64_t)(diffLon * diffLon) * 4;
                if(distanceSqr < *distanceSqrMin) *distanceSqrMin = distanceSqr;
            }
        }

        prevQuadrant = quadrant;
        prevLat = pointLat;
        prevLon = pointLon;
        first = 0;
    };

    if(winding == -4) {
        return ZD_LOOKUP_IN_ZONE;
    } else if(winding == 4) {
        return ZD_LOOKUP_IN_EXCLUDED_ZONE;
    } else if(winding == 0) {
        return ZD_LOOKUP_NOT_IN_ZONE;
    }

    /* Should not happen */
    if(distanceSqrMin) *distanceSqrMin = 0;
    return ZD_LOOKUP_ON_BORDER_SEGMENT;
}

void ZDCloseDatabase(ZoneDetect *library)
{
    if(library) {
        if(library->fieldNames) {
            size_t i;
            for(i = 0; i < (size_t)library->numFields; i++) {
                if(library->fieldNames[i]) {
                    free(library->fieldNames[i]);
                }
            }
            free(library->fieldNames);
        }
        if(library->notice) {
            free(library->notice);
        }

        if(library->closeType == 0) {
#if defined(_MSC_VER) || defined(__MINGW32__)
            if(library->mapping && !UnmapViewOfFile(library->mapping)) zdError(ZD_E_DB_MUNMAP_MSVIEW, (int)GetLastError());
            if(library->fdMap && !CloseHandle(library->fdMap))         zdError(ZD_E_DB_MUNMAP, (int)GetLastError());
            if(library->fd && !CloseHandle(library->fd))               zdError(ZD_E_DB_CLOSE, (int)GetLastError());
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(_POSIX_VERSION)
            if(library->mapping && munmap(library->mapping, (size_t)(library->length))) zdError(ZD_E_DB_MUNMAP, 0);
            if(library->fd >= 0 && close(library->fd))                                  zdError(ZD_E_DB_CLOSE, 0);
#endif
        }

        free(library);
    }
}

ZoneDetect *ZDOpenDatabaseFromMemory(void* buffer, size_t length)
{
    ZoneDetect *const library = malloc(sizeof *library);

    if(library) {
        memset(library, 0, sizeof(*library));
        library->closeType = 1;
        library->length = (long int)length;

        if(library->length <= 0) {
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(_POSIX_VERSION)
            zdError(ZD_E_DB_SEEK, errno);
#else
            zdError(ZD_E_DB_SEEK, 0);
#endif
            goto fail;
        }

        library->mapping = buffer;

        /* Parse the header */
        if(ZDParseHeader(library)) {
            zdError(ZD_E_PARSE_HEADER, 0);
            goto fail;
        }
    }

    return library;

fail:
    ZDCloseDatabase(library);
    return NULL;
}

ZoneDetect *ZDOpenDatabase(const char *path)
{
    ZoneDetect *const library = malloc(sizeof *library);

    if(library) {
        memset(library, 0, sizeof(*library));

#if defined(_MSC_VER) || defined(__MINGW32__)
        library->fd = CreateFile(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (library->fd == INVALID_HANDLE_VALUE) {
            zdError(ZD_E_DB_OPEN, (int)GetLastError());
            goto fail;
        }

        const DWORD fsize = GetFileSize(library->fd, NULL);
        if (fsize == INVALID_FILE_SIZE) {
            zdError(ZD_E_DB_SEEK, (int)GetLastError());
            goto fail;
        }
        library->length = (int32_t)fsize;

        library->fdMap = CreateFileMappingA(library->fd, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!library->fdMap) {
            zdError(ZD_E_DB_MMAP, (int)GetLastError());
            goto fail;
        }

        library->mapping = MapViewOfFile(library->fdMap, FILE_MAP_READ, 0, 0, 0);
        if (!library->mapping) {
            zdError(ZD_E_DB_MMAP_MSVIEW, (int)GetLastError());
            goto fail;
        }
#elif defined(__APPLE__) || defined(__linux__) || defined(__unix__) || defined(_POSIX_VERSION)
        library->fd = open(path, O_RDONLY | O_CLOEXEC);
        if(library->fd < 0) {
            zdError(ZD_E_DB_OPEN, errno);
            goto fail;
        }

        library->length = lseek(library->fd, 0, SEEK_END);
        if(library->length <= 0 || library->length > 50331648) {
            zdError(ZD_E_DB_SEEK, errno);
            goto fail;
        }
        lseek(library->fd, 0, SEEK_SET);

        library->mapping = mmap(NULL, (size_t)library->length, PROT_READ, MAP_PRIVATE | MAP_FILE, library->fd, 0);
        if(library->mapping == MAP_FAILED) {
            zdError(ZD_E_DB_MMAP, errno);
            goto fail;
        }
#endif

        /* Parse the header */
        if(ZDParseHeader(library)) {
            zdError(ZD_E_PARSE_HEADER, 0);
            goto fail;
        }
    }

    return library;

fail:
    ZDCloseDatabase(library);
    return NULL;
}

ZoneDetectResult *ZDLookup(const ZoneDetect *library, float lat, float lon, float *safezone)
{
    const int32_t latFixedPoint = ZDFloatToFixedPoint(lat, 90, library->precision);
    const int32_t lonFixedPoint = ZDFloatToFixedPoint(lon, 180, library->precision);
    size_t numResults = 0;
    uint64_t distanceSqrMin = (uint64_t)-1;

    /* Iterate over all polygons */
    uint32_t bboxIndex = library->bboxOffset;
    uint32_t metadataIndex = 0;
    uint32_t polygonIndex = 0;

    ZoneDetectResult *results = malloc(sizeof *results);
    if(!results) {
        return NULL;
    }

    uint32_t polygonId = 0;

    while(bboxIndex < library->metadataOffset) {
        int32_t minLat, minLon, maxLat, maxLon, metadataIndexDelta;
        uint64_t polygonIndexDelta;
        if(!ZDDecodeVariableLengthSigned(library, &bboxIndex, &minLat)) break;
        if(!ZDDecodeVariableLengthSigned(library, &bboxIndex, &minLon)) break;
        if(!ZDDecodeVariableLengthSigned(library, &bboxIndex, &maxLat)) break;
        if(!ZDDecodeVariableLengthSigned(library, &bboxIndex, &maxLon)) break;
        if(!ZDDecodeVariableLengthSigned(library, &bboxIndex, &metadataIndexDelta)) break;
        if(!ZDDecodeVariableLengthUnsigned(library, &bboxIndex, &polygonIndexDelta)) break;

        metadataIndex += (uint32_t)metadataIndexDelta;
        polygonIndex += (uint32_t)polygonIndexDelta;

        if(latFixedPoint >= minLat) {
            if(latFixedPoint <= maxLat &&
                    lonFixedPoint >= minLon &&
                    lonFixedPoint <= maxLon) {

                const ZDLookupResult lookupResult = ZDPointInPolygon(library, library->dataOffset + polygonIndex, latFixedPoint, lonFixedPoint, (safezone) ? &distanceSqrMin : NULL);
                if(lookupResult == ZD_LOOKUP_PARSE_ERROR) {
                    break;
                } else if(lookupResult != ZD_LOOKUP_NOT_IN_ZONE) {
                    ZoneDetectResult *const newResults = realloc(results, sizeof *newResults * (numResults + 2));

                    if(newResults) {
                        results = newResults;
                        results[numResults].polygonId = polygonId;
                        results[numResults].metaId = metadataIndex;
                        results[numResults].numFields = library->numFields;
                        results[numResults].fieldNames = library->fieldNames;
                        results[numResults].lookupResult = lookupResult;

                        numResults++;
                    } else {
                        break;
                    }
                }
            }
        } else {
            /* The data is sorted along minLat */
            break;
        }

        polygonId++;
    }

    /* Clean up results */
    size_t i;
    for(i = 0; i < numResults; i++) {
        int insideSum = 0;
        ZDLookupResult overrideResult = ZD_LOOKUP_IGNORE;
        size_t j;
        for(j = i; j < numResults; j++) {
            if(results[i].metaId == results[j].metaId) {
                ZDLookupResult tmpResult = results[j].lookupResult;
                results[j].lookupResult = ZD_LOOKUP_IGNORE;

                /* This is the same result. Is it an exclusion zone? */
                if(tmpResult == ZD_LOOKUP_IN_ZONE) {
                    insideSum++;
                } else if(tmpResult == ZD_LOOKUP_IN_EXCLUDED_ZONE) {
                    insideSum--;
                } else {
                    /* If on the bodrder then the final result is on the border */
                    overrideResult = tmpResult;
                }

            }
        }

        if(overrideResult != ZD_LOOKUP_IGNORE) {
            results[i].lookupResult = overrideResult;
        } else {
            if(insideSum) {
                results[i].lookupResult = ZD_LOOKUP_IN_ZONE;
            }
        }
    }

    /* Remove zones to ignore */
    size_t newNumResults = 0;
    for(i = 0; i < numResults; i++) {
        if(results[i].lookupResult != ZD_LOOKUP_IGNORE) {
            results[newNumResults] = results[i];
            newNumResults++;
        }
    }
    numResults = newNumResults;

    /* Lookup metadata */
    for(i = 0; i < numResults; i++) {
        uint32_t tmpIndex = library->metadataOffset + results[i].metaId;
        results[i].data = malloc(library->numFields * sizeof *results[i].data);
        if(results[i].data) {
            size_t j;
            for(j = 0; j < library->numFields; j++) {
                results[i].data[j] = ZDParseString(library, &tmpIndex);
                if (!results[i].data[j]) {
                    /* free all allocated memory */
                    size_t m;
                    for(m = 0; m < j; m++) {
                        if(results[i].data[m]) {
                            free(results[i].data[m]);
                        }
                    }
                    size_t k;
                    for(k = 0; k < i; k++) {
                        size_t l;
                        for(l = 0; l < (size_t)results[k].numFields; l++) {
                            if(results[k].data[l]) {
                                free(results[k].data[l]);
                            }
                        }
                        if (results[k].data) {
                            free(results[k].data);
                        }
                    }
                    free(results);
                    return NULL;
                }
            }
        }
        else {
            /* free all allocated memory */
            size_t k;
            for(k = 0; k < i; k++) {
                size_t l;
                for(l = 0; l < (size_t)results[k].numFields; l++) {
                    if(results[k].data[l]) {
                        free(results[k].data[l]);
                    }
                }
                if (results[k].data) {
                    free(results[k].data);
                }
            }
            free(results);
            return NULL;
        }
    }

    /* Write end marker */
    results[numResults].lookupResult = ZD_LOOKUP_END;
    results[numResults].numFields = 0;
    results[numResults].fieldNames = NULL;
    results[numResults].data = NULL;

    if(safezone) {
        *safezone = sqrtf((float)distanceSqrMin) * 90 / (float)(1 << (library->precision - 1));
    }

    return results;
}

void ZDFreeResults(ZoneDetectResult *results)
{
    unsigned int index = 0;

    if(!results) {
        return;
    }

    while(results[index].lookupResult != ZD_LOOKUP_END) {
        if(results[index].data) {
            size_t i;
            for(i = 0; i < (size_t)results[index].numFields; i++) {
                if(results[index].data[i]) {
                    free(results[index].data[i]);
                }
            }
            free(results[index].data);
        }
        index++;
    }
    free(results);
}

const char *ZDGetNotice(const ZoneDetect *library)
{
    return library->notice;
}

uint8_t ZDGetTableType(const ZoneDetect *library)
{
    return library->tableType;
}

const char *ZDLookupResultToString(ZDLookupResult result)
{
    switch(result) {
        case ZD_LOOKUP_IGNORE:
            return "Ignore";
        case ZD_LOOKUP_END:
            return "End";
        case ZD_LOOKUP_PARSE_ERROR:
            return "Parsing error";
        case ZD_LOOKUP_NOT_IN_ZONE:
            return "Not in zone";
        case ZD_LOOKUP_IN_ZONE:
            return "In zone";
        case ZD_LOOKUP_IN_EXCLUDED_ZONE:
            return "In excluded zone";
        case ZD_LOOKUP_ON_BORDER_VERTEX:
            return "Target point is border vertex";
        case ZD_LOOKUP_ON_BORDER_SEGMENT:
            return "Target point is on border";
    }

    return "Unknown";
}

#define ZD_E_COULD_NOT(msg) "could not " msg

const char *ZDGetErrorString(int errZD)
{
    switch ((enum ZDInternalError)errZD) {
        default:
            assert(0);
        case ZD_OK                :
            return "";
        case ZD_E_DB_OPEN         :
            return ZD_E_COULD_NOT("open database file");
        case ZD_E_DB_SEEK         :
            return ZD_E_COULD_NOT("retrieve database file size");
        case ZD_E_DB_MMAP         :
            return ZD_E_COULD_NOT("map database file to system memory");
#if defined(_MSC_VER) || defined(__MINGW32__)
        case ZD_E_DB_MMAP_MSVIEW  :
            return ZD_E_COULD_NOT("open database file view");
        case ZD_E_DB_MAP_EXCEPTION:
            return "I/O exception occurred while accessing database file view";
        case ZD_E_DB_MUNMAP_MSVIEW:
            return ZD_E_COULD_NOT("close database file view");
#endif
        case ZD_E_DB_MUNMAP       :
            return ZD_E_COULD_NOT("unmap database");
        case ZD_E_DB_CLOSE        :
            return ZD_E_COULD_NOT("close database file");
        case ZD_E_PARSE_HEADER    :
            return ZD_E_COULD_NOT("parse database header");
    }
}

#undef ZD_E_COULD_NOT

int ZDSetErrorHandler(void (*handler)(int, int))
{
    zdErrorHandler = handler;
    return 0;
}

char* ZDHelperSimpleLookupString(const ZoneDetect* library, float lat, float lon)
{
    ZoneDetectResult *result = ZDLookup(library, lat, lon, NULL);
    if(!result) {
        return NULL;
    }

    char* output = NULL;

    if(result[0].lookupResult == ZD_LOOKUP_END) {
        goto done;
    }

    char* strings[2] = {NULL};

    unsigned int i;
    for(i = 0; i < result[0].numFields; i++) {
        if(result[0].fieldNames[i] && result[0].data[i]) {
            if(library->tableType == 'T') {
                if(!strcmp(result[0].fieldNames[i], "TimezoneIdPrefix")) {
                    strings[0] = result[0].data[i];
                }
                if(!strcmp(result[0].fieldNames[i], "TimezoneId")) {
                    strings[1] = result[0].data[i];
                }
            }
            if(library->tableType == 'C') {
                if(!strcmp(result[0].fieldNames[i], "Name")) {
                    strings[0] = result[0].data[i];
                }
            }
        }
    }

    size_t length = 0;
    for(i=0; i<sizeof(strings)/sizeof(char*); i++) {
        if(strings[i]) {
            size_t partLength = strlen(strings[i]);
            if(partLength > 512) {
                goto done;
            }
            length += partLength;
        }
    }

    if(length == 0) {
        goto done;
    }

    length += 1;

    output = (char*)malloc(length);
    if(output) {
        output[0] = 0;
        for(i=0; i<sizeof(strings)/sizeof(char*); i++) {
            if(strings[i]) {
#if defined(_MSC_VER)
                strcat_s(output + strlen(output), length-strlen(output), strings[i]);
#else
                strcat(output + strlen(output), strings[i]);
#endif
            }
        }
    }

done:
    ZDFreeResults(result);
    return output;
}

void ZDHelperSimpleLookupStringFree(char* str)
{
    free(str);
}