
/**
 * @file
 * Contains common functions used when showing a map.
 */

import { debug } from "./debug.js";
import { getCosmeticName, getCosmeticLocation, getBaseTopic, escapeHTML } from "./misc.js";

/**
 * Loads a map solution. The solution is Google Maps if an API key is present,
 * or a Leaflet map if no API key is present.
 * When the map solution has been loaded, an `initialize` function is called
 * from the local `map_google.js` or `map_leaflet.js` file, depending on which
 * solution was loaded.
 * 
 * @param  {...any} initializerArguments - Optional arguments to pass to the `initialize` function.
 */
export async function loadMapSolution(...initializerArguments){

  debug("Loading map solution");
  if(apiKey){
    debug("Found API key, using Google Maps");
    debug("API key:", apiKey);

    const initialize = await getLocalMapInitializer("google");

    const script = document.createElement('script');
    script.src = `https://maps.googleapis.com/maps/api/js?v=3&key=${ apiKey }`;
    script.addEventListener("load", () => {initialize(...initializerArguments);});
    document.querySelector("head").appendChild(script);

  }else{
    debug("Did not find API key, using Leaflet map");

    const initialize = await getLocalMapInitializer("leaflet");

    const style = document.createElement("link");
    style.rel = "stylesheet";
    style.href = "../static/leaflet/leaflet.css";
    document.querySelector("head").appendChild(style);

    const script = document.createElement('script');
    script.src = "../static/leaflet/leaflet.js";
    script.addEventListener("load", () => {initialize(...initializerArguments);});
    document.querySelector("head").appendChild(script);

  }

  async function getLocalMapInitializer(solution){
    const { initialize } = await import(new URL(`map_${solution}.js`, window.location));
    return initialize;
  }
}

/**
 * Creates an HTML representation of the "loc" (location) element obtained from the recorder API
 * Intended to be used in a map marker popup.
 * 
 * @param {object} loc - A location element obtained from the recorder API.
 * @returns {string} - An HTML string that can be used in a map marker popup.
 */
export function generatePopupHTML(
  {
    face,
    topic,
    name,
    label,
    tid,
    addr,
    lat,
    lon,
    ghash,
    acc,
    tst,
    batt,
    vel,
    cog,
  },
){
  let html = "";

  // Face
  if(face !== undefined) html += escapeHTML `<img class='face' alt='' src='data:image/png;base64,${ face.replaceAll("'", "") }' height='35' width='35'>`;

  // Name
  html += escapeHTML `<span class="name"><b>${ getCosmeticName({topic, name, label, tid}) }</b><br></span>`;

  // Location
  html += escapeHTML `<span class="location">${ getCosmeticLocation({addr, lat, lon}) }<br></span>`;

  // Ghash + base topic
  const baseTopic = getBaseTopic(topic);

  html += escapeHTML `<span class='low-level'>${ ghash || "unknown" }, ${ baseTopic || "unknown" }`;

  // Date
  if(tst !== undefined){
    const date = new Date(tst * 1000).toLocaleDateString(undefined, {
      day: 'numeric',
      month: 'short',
      year: 'numeric',
      hour: 'numeric',
      minute: 'numeric',
      second: 'numeric',
      timeZoneName: 'short',
    });
    html += `, </span><span class="date">${date}`;
  }
  html += "<br></span>";

  // Latitude and longtitude
  html += escapeHTML `<span class="lat-lon">(${lat},${lon}), </span>`;

  // Miscellaneous
  html += "<span class='misc'>";
  const misc = [];

  misc.push(`acc: ${ acc === undefined ? "?" : `${acc} m` }`);

  if(vel !== undefined) misc.push(escapeHTML `vel: ${vel} km/h`);
  if(batt !== undefined) misc.push(escapeHTML `batt: ${batt}%`);
  if(cog !== undefined) misc.push(escapeHTML `cog: ${cog}`);

  html += misc.join(", ");
  html += "</span>";

  return html;
}
