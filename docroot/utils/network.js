
import { debug } from "./debug.js";

/**
 * Generate a URL to a specific recorder API endpoint.
 * 
 * @param {string} endpoint - The name of the API endpoint.
 * @param {boolean} options.useWebsocket - Connect over the websocket protocol instead of standard HTTP.
 * @param {boolean} options.includeSearchParams - Copy over any search parameters that are defined on the page URL.
 * 
 * @returns {URL} - A full URL for the API endpoint.
 */
export function getApiUrl(endpoint, { useWebsocket = false, includeSearchParams = false }){

  const apiUrl = new URL(`../${ useWebsocket ? "ws" : "api/0" }${endpoint === undefined ? "" : `/${endpoint}`}`, window.location);

  if(useWebsocket) apiUrl.protocol = window.location.protocol === "https:" ? "wss:" : "ws:";

  if(includeSearchParams) apiUrl.search = window.location.search;

  return apiUrl;
}

/**
 * Connects to the recorder API and fetches data.
 * Can either take a full URL to the API endpoint (use the `url` option),
 * or connect to a named endpoint (use the `endpoint` option).
 * 
 * When connecting to a named endpoint, the URL is created with `getApiUrl`.
 * 
 * @param {RequestInfo} options.url - A full URL/Request to the API endpoint.
 * @param {string} options.endpoint - The name of the API endpoint.
 * @param {boolean} options.includeSearchParams - Copy over any search parameters that are defined on the page URL.
 * 
 * @returns {Promise<object>} - The data returned by the API.
 */
export async function fetchApiData({ url, endpoint, includeSearchParams }){

  if(url === undefined){
    url = getApiUrl(endpoint, { includeSearchParams });
  }

  debug("Connecting to API endpoint:", url);

  const data = fetch(url)
    .then(async response => {
      if (!response.ok) {
        throw new Error('API response is not "ok".');
      }
      const data = await response.json();
      debug("Response from API:", data);
      return data;
    })
    .catch(error => {
      console.error(error);
      throw new Error("Unable to connect to API.");
    });

  return data;
}
