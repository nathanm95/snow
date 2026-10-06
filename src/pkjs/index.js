var Clay = require('@rebble/clay');
var clayConfig = require('./config');

// Clay renders the page, stores the answers on the phone, and posts them to
// the watch as an AppMessage keyed by the messageKeys in package.json.
var clay = new Clay(clayConfig, null, { autoHandleEvents: true });

Pebble.addEventListener('ready', function () {
  console.log('Snow: PebbleKit JS ready');
});
