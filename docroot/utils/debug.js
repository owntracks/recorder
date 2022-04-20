

/**
 * When this is set to true, all frontend pages will start loggin debug information to the browser console.
 */
export const DEBUG = false;

/**
 * Logs debug information to the browser console if debug mode is active.
 * @param  {...any} args - The arguments you would normally pass to `console.log`.
 */
export function debug(...args){
  if(DEBUG) console.log(...args);
}
