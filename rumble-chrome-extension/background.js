/*global chrome: true */


/**
 * Assigns a unique (to the current session) integer to each
 * page load, appending it to the headers of every request
 * as X-Rumble-Request-Id.
 */


var nextId = 0;
var idsByTab = {};

// details: requestId, url, method, frameId, parentFrameId, tabId, type, timeStamp, requestHeaders
// details.type: ["main_frame", "sub_frame", "stylesheet", "script", "image", "object", "xmlhttprequest", "other"]
chrome.webRequest.onBeforeSendHeaders.addListener(function (details) {
    "use strict";
    
    // increment the ID if the browser is visiting a new page
    if (details.type === "main_frame") {
        idsByTab[details.tabId] = nextId;
        nextId += 1;
    }
    
    details.requestHeaders.push({ name: "X-Rumble-Request-Id", value: idsByTab[details.tabId].toString() });
    
    return { requestHeaders: details.requestHeaders };
}, { urls: ["<all_urls>"] }, ["blocking", "requestHeaders"]);
