#ifndef _RECORDER_H_INCL_
# define _RECORDER_H_INCL_

void handle_message(void *userdata, char *topic, char *payload, size_t payloadlen, int retain, int httpmode, int was_encrypted);

#endif /* _RECORDER_H_INCL_ */
