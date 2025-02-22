/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

this.MAIN_MESSAGE_TYPE = "ActivityStream:Main";
this.CONTENT_MESSAGE_TYPE = "ActivityStream:Content";
this.UI_CODE = 1;
this.BACKGROUND_PROCESS = 2;

/**
 * globalImportContext - Are we in UI code (i.e. react, a dom) or some kind of background process?
 *                       Use this in action creators if you need different logic
 *                       for ui/background processes.
 */
const globalImportContext = typeof Window === "undefined" ? BACKGROUND_PROCESS : UI_CODE;
// Export for tests
this.globalImportContext = globalImportContext;

// Create an object that avoids accidental differing key/value pairs:
// {
//   INIT: "INIT",
//   UNINIT: "UNINIT"
// }
const actionTypes = {};
for (const type of [
  "BLOCK_URL",
  "BOOKMARK_URL",
  "DELETE_BOOKMARK_BY_ID",
  "DELETE_HISTORY_URL",
  "DELETE_HISTORY_URL_CONFIRM",
  "DIALOG_CANCEL",
  "DIALOG_OPEN",
  "DISABLE_ONBOARDING",
  "INIT",
  "MIGRATION_CANCEL",
  "MIGRATION_COMPLETED",
  "MIGRATION_START",
  "NEW_TAB_INIT",
  "NEW_TAB_INITIAL_STATE",
  "NEW_TAB_LOAD",
  "NEW_TAB_REHYDRATED",
  "NEW_TAB_STATE_REQUEST",
  "NEW_TAB_UNLOAD",
  "OPEN_LINK",
  "OPEN_NEW_WINDOW",
  "OPEN_PRIVATE_WINDOW",
  "PAGE_PRERENDERED",
  "PLACES_BOOKMARK_ADDED",
  "PLACES_BOOKMARK_CHANGED",
  "PLACES_BOOKMARK_REMOVED",
  "PLACES_HISTORY_CLEARED",
  "PLACES_LINKS_DELETED",
  "PLACES_LINK_BLOCKED",
  "PREFS_INITIAL_VALUES",
  "PREF_CHANGED",
  "SAVE_SESSION_PERF_DATA",
  "SAVE_TO_POCKET",
  "SCREENSHOT_UPDATED",
  "SECTION_DEREGISTER",
  "SECTION_DISABLE",
  "SECTION_ENABLE",
  "SECTION_OPTIONS_CHANGED",
  "SECTION_REGISTER",
  "SECTION_UPDATE",
  "SECTION_UPDATE_CARD",
  "SETTINGS_CLOSE",
  "SETTINGS_OPEN",
  "SET_PREF",
  "SHOW_FIREFOX_ACCOUNTS",
  "SNIPPETS_DATA",
  "SNIPPETS_RESET",
  "SYSTEM_TICK",
  "TELEMETRY_IMPRESSION_STATS",
  "TELEMETRY_PERFORMANCE_EVENT",
  "TELEMETRY_UNDESIRED_EVENT",
  "TELEMETRY_USER_EVENT",
  "TOP_SITES_ADD",
  "TOP_SITES_CANCEL_EDIT",
  "TOP_SITES_EDIT",
  "TOP_SITES_PIN",
  "TOP_SITES_UNPIN",
  "TOP_SITES_UPDATED",
  "UNINIT"
]) {
  actionTypes[type] = type;
}

// Helper function for creating routed actions between content and main
// Not intended to be used by consumers
function _RouteMessage(action, options) {
  const meta = action.meta ? Object.assign({}, action.meta) : {};
  if (!options || !options.from || !options.to) {
    throw new Error("Routed Messages must have options as the second parameter, and must at least include a .from and .to property.");
  }
  // For each of these fields, if they are passed as an option,
  // add them to the action. If they are not defined, remove them.
  ["from", "to", "toTarget", "fromTarget", "skipOrigin"].forEach(o => {
    if (typeof options[o] !== "undefined") {
      meta[o] = options[o];
    } else if (meta[o]) {
      delete meta[o];
    }
  });
  return Object.assign({}, action, {meta});
}

/**
 * SendToMain - Creates a message that will be sent to the Main process.
 *
 * @param  {object} action Any redux action (required)
 * @param  {object} options
 * @param  {string} fromTarget The id of the content port from which the action originated. (optional)
 * @return {object} An action with added .meta properties
 */
function SendToMain(action, fromTarget) {
  return _RouteMessage(action, {
    from: CONTENT_MESSAGE_TYPE,
    to: MAIN_MESSAGE_TYPE,
    fromTarget
  });
}

/**
 * BroadcastToContent - Creates a message that will be sent to ALL content processes.
 *
 * @param  {object} action Any redux action (required)
 * @return {object} An action with added .meta properties
 */
function BroadcastToContent(action) {
  return _RouteMessage(action, {
    from: MAIN_MESSAGE_TYPE,
    to: CONTENT_MESSAGE_TYPE
  });
}

/**
 * SendToContent - Creates a message that will be sent to a particular Content process.
 *
 * @param  {object} action Any redux action (required)
 * @param  {string} target The id of a content port
 * @return {object} An action with added .meta properties
 */
function SendToContent(action, target) {
  if (!target) {
    throw new Error("You must provide a target ID as the second parameter of SendToContent. If you want to send to all content processes, use BroadcastToContent");
  }
  return _RouteMessage(action, {
    from: MAIN_MESSAGE_TYPE,
    to: CONTENT_MESSAGE_TYPE,
    toTarget: target
  });
}

/**
 * UserEvent - A telemetry ping indicating a user action. This should only
 *                   be sent from the UI during a user session.
 *
 * @param  {object} data Fields to include in the ping (source, etc.)
 * @return {object} An SendToMain action
 */
function UserEvent(data) {
  return SendToMain({
    type: actionTypes.TELEMETRY_USER_EVENT,
    data
  });
}

/**
 * UndesiredEvent - A telemetry ping indicating an undesired state.
 *
 * @param  {object} data Fields to include in the ping (value, etc.)
 * @param  {int} importContext (For testing) Override the import context for testing.
 * @return {object} An action. For UI code, a SendToMain action.
 */
function UndesiredEvent(data, importContext = globalImportContext) {
  const action = {
    type: actionTypes.TELEMETRY_UNDESIRED_EVENT,
    data
  };
  return importContext === UI_CODE ? SendToMain(action) : action;
}

/**
 * PerfEvent - A telemetry ping indicating a performance-related event.
 *
 * @param  {object} data Fields to include in the ping (value, etc.)
 * @param  {int} importContext (For testing) Override the import context for testing.
 * @return {object} An action. For UI code, a SendToMain action.
 */
function PerfEvent(data, importContext = globalImportContext) {
  const action = {
    type: actionTypes.TELEMETRY_PERFORMANCE_EVENT,
    data
  };
  return importContext === UI_CODE ? SendToMain(action) : action;
}

/**
 * ImpressionStats - A telemetry ping indicating an impression stats.
 *
 * @param  {object} data Fields to include in the ping
 * @param  {int} importContext (For testing) Override the import context for testing.
 * #return {object} An action. For UI code, a SendToMain action.
 */
function ImpressionStats(data, importContext = globalImportContext) {
  const action = {
    type: actionTypes.TELEMETRY_IMPRESSION_STATS,
    data
  };
  return importContext === UI_CODE ? SendToMain(action) : action;
}

function SetPref(name, value, importContext = globalImportContext) {
  const action = {type: actionTypes.SET_PREF, data: {name, value}};
  return importContext === UI_CODE ? SendToMain(action) : action;
}

this.actionTypes = actionTypes;

this.actionCreators = {
  BroadcastToContent,
  UserEvent,
  UndesiredEvent,
  PerfEvent,
  ImpressionStats,
  SendToContent,
  SendToMain,
  SetPref
};

// These are helpers to test for certain kinds of actions
this.actionUtils = {
  isSendToMain(action) {
    if (!action.meta) {
      return false;
    }
    return action.meta.to === MAIN_MESSAGE_TYPE && action.meta.from === CONTENT_MESSAGE_TYPE;
  },
  isBroadcastToContent(action) {
    if (!action.meta) {
      return false;
    }
    if (action.meta.to === CONTENT_MESSAGE_TYPE && !action.meta.toTarget) {
      return true;
    }
    return false;
  },
  isSendToContent(action) {
    if (!action.meta) {
      return false;
    }
    if (action.meta.to === CONTENT_MESSAGE_TYPE && action.meta.toTarget) {
      return true;
    }
    return false;
  },
  isFromMain(action) {
    if (!action.meta) {
      return false;
    }
    return action.meta.from === MAIN_MESSAGE_TYPE &&
      action.meta.to === CONTENT_MESSAGE_TYPE;
  },
  getPortIdOfSender(action) {
    return (action.meta && action.meta.fromTarget) || null;
  },
  _RouteMessage
};

this.EXPORTED_SYMBOLS = [
  "actionTypes",
  "actionCreators",
  "actionUtils",
  "globalImportContext",
  "UI_CODE",
  "BACKGROUND_PROCESS",
  "MAIN_MESSAGE_TYPE",
  "CONTENT_MESSAGE_TYPE"
];
