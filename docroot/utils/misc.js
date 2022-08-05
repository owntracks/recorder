
import { renames } from "../utils/config.js";

/**
 * Use this as a template literal tag to escape all variables for use as text inside HTML elements.
 */
export function escapeHTML(text, ...values){
    
  // Escape values
  const escaped = values.map(value => String(value).replaceAll("<", "&lt;").replaceAll(">", "&gt;"));

  // Combine final string
  let result = text[0];
  for(const [ index, value ] of escaped.entries()){
    result += value;
    result += text[index + 1];
  }

  return result;
}

/**
 * Finds the nicest available name in an `loc` (location) object obtained from the recorder API.
 * 
 * @param {object} loc - The location object obtained from the recorder API.
 * @returns {string} - A readable name for the user/device that issued the `loc`.
 */
export function getCosmeticName(
  {
    topic,
    name,
    label,
    tid,
  },
){
  const baseTopic = getBaseTopic(topic);

  return String(renames[baseTopic] || name || label || tid || baseTopic || "anonymous");
}

/**
 * Finds the nicest available location name in an `loc` (location) object obtained from the recorder API.
 * 
 * @param {object} loc - The location object obtained from the recorder API.
 * @returns {string} - A readable name for the location that the `loc` element represents.
 */
export function getCosmeticLocation(
  {
    addr,
    lat,
    lon,
  },
){
  return String(addr || `${ lat }, ${ lon }`);
}

/**
 * Finds the base topic in a full topic string.
 * 
 * The base topic consists of the username and device id.
 * The original topic string is expected to be formatted as:
 * "{ recorder topic }/{ username }/{ device id }/....."
 * 
 * @param {string} topic - The full topic string.
 * @returns {string} - The base topic on the form: "{ username }/{ device id }".
 */
export function getBaseTopic(topic){
  if(!topic) return undefined;

  const topicArray = topic.split('/');
  if(topicArray[0] === "") topicArray.shift(); /* cater for leading '/' in topic string */

  if(topicArray.length < 3) return undefined;

  return `${ topicArray[1] }/${ topicArray[2] }`; /* "{ username }/{ device id }" */
}
