/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  Component,
  DOM,
  PropTypes,
} = require("devtools/client/shared/vendor/react");
const { L10N } = require("../utils/l10n");
const { propertiesEqual } = require("../utils/request-utils");

const { div } = DOM;

const UPDATED_STATUS_PROPS = [
  "fromCache",
  "fromServiceWorker",
  "status",
  "statusText",
];

class RequestListColumnStatus extends Component {
  static get propTypes() {
    return {
      item: PropTypes.object.isRequired,
    };
  }

  shouldComponentUpdate(nextProps) {
    return !propertiesEqual(UPDATED_STATUS_PROPS, this.props.item, nextProps.item);
  }

  render() {
    let { item } = this.props;
    let { fromCache, fromServiceWorker, status, statusText } = item;
    let code;

    if (status) {
      if (fromCache) {
        code = "cached";
      } else if (fromServiceWorker) {
        code = "service worker";
      } else {
        code = status;
      }
    }

    return (
      div({
        className: "requests-list-column requests-list-status",
        onMouseOver: function ({ target }) {
          if (status && statusText && !target.title) {
            target.title = getColumnTitle(item);
          }
        },
      },
      div({ className: "requests-list-status-icon", "data-code": code }),
        div({ className: "requests-list-status-code" }, status)
      )
    );
  }
}

function getColumnTitle(item) {
  let { fromCache, fromServiceWorker, status, statusText } = item;
  let title;
  if (fromCache && fromServiceWorker) {
    title = L10N.getFormatStr("netmonitor.status.tooltip.cachedworker",
      status, statusText);
  } else if (fromCache) {
    title = L10N.getFormatStr("netmonitor.status.tooltip.cached",
      status, statusText);
  } else if (fromServiceWorker) {
    title = L10N.getFormatStr("netmonitor.status.tooltip.worker",
      status, statusText);
  } else {
    title = L10N.getFormatStr("netmonitor.status.tooltip.simple",
      status, statusText);
  }
  return title;
}

module.exports = RequestListColumnStatus;
