// Clay configuration page for Snow.
//
// Keys must match `messageKeys` in package.json and the MESSAGE_KEY_* reads in
// src/c/settings.c. Defaults repeat settings.c so a first-time page shows what
// the watch is actually doing.

module.exports = [
  {
    type: 'heading',
    defaultValue: 'Snow',
    size: 1
  },
  {
    type: 'text',
    defaultValue:
      'A snow globe on your wrist. Snow falls with gravity and piles up on ' +
      'the time — tilt your wrist to tip it, flick it to stir it up.'
  },

  // ------------------------------------------------------------- Clock ----
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Clock' },
      {
        type: 'select',
        messageKey: 'ClockSize',
        label: 'Clock size',
        defaultValue: '0',
        options: [
          { label: 'Classic', value: '0' },
          { label: 'Large', value: '1' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'ShowDate',
        label: 'Show the date',
        description: 'Snow settles on the date too.',
        defaultValue: false
      }
    ]
  },

  // ------------------------------------------------------------ Colours ---
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Colours' },
      {
        type: 'color',
        messageKey: 'ColBg',
        label: 'Sky',
        defaultValue: '000000',
        sunlight: true,
        capabilities: ['COLOR']
      },
      {
        type: 'color',
        messageKey: 'ColSnow',
        label: 'Snow',
        defaultValue: 'FFFFFF',
        sunlight: true,
        capabilities: ['COLOR']
      },
      {
        type: 'color',
        messageKey: 'ColTime',
        label: 'Clock',
        defaultValue: 'FFFFFF',
        sunlight: true,
        capabilities: ['COLOR']
      },
      {
        type: 'toggle',
        messageKey: 'Invert',
        label: 'Black snow on white',
        defaultValue: false,
        capabilities: ['BW']
      }
    ]
  },

  // --------------------------------------------------------------- Snow ---
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Snow' },
      {
        type: 'select',
        messageKey: 'Density',
        label: 'How much snow',
        defaultValue: '1',
        options: [
          { label: 'Light', value: '0' },
          { label: 'Normal', value: '1' },
          { label: 'Heavy', value: '2' },
          { label: 'Blizzard', value: '3' }
        ]
      },
      {
        type: 'toggle',
        messageKey: 'BatterySnow',
        label: 'Snow shows battery level',
        description:
          'A full battery is a full globe. The snow melts away as the ' +
          'battery runs down, and falls again while charging.',
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'Endless',
        label: 'Endless snowfall',
        description:
          'The pile slowly melts from the bottom while fresh snow keeps ' +
          'falling. Always animating, so it uses more battery.',
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'Wind',
        label: 'Wind gusts',
        description: 'Now and then a gust blows the falling snow sideways into drifts.',
        defaultValue: false
      },
      {
        type: 'toggle',
        messageKey: 'HourlyFlurry',
        label: 'Hourly flurry',
        description: 'The globe gives itself a shake on the hour.',
        defaultValue: false
      }
    ]
  },

  // ------------------------------------------------------------- Motion ---
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Motion' },
      {
        type: 'toggle',
        messageKey: 'Tilt',
        label: 'Tilt moves the snow',
        description: 'Off: snow always falls straight down the screen.',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'ShakeStir',
        label: 'Flick to stir',
        description: 'A flick of the wrist throws the snow back up.',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'ShakeLight',
        label: 'Flick turns on the light',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'TouchSweep',
        label: 'Sweep with your finger',
        description: 'Drag a finger across the screen to brush the snow aside.',
        defaultValue: true,
        capabilities: ['TOUCH']
      }
    ]
  },

  // ------------------------------------------------------------ Battery ---
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Battery' },
      {
        type: 'toggle',
        messageKey: 'BatterySaver',
        label: 'Rest when settled',
        description:
          'Stop animating once the snow has landed, and wake on a tilt, ' +
          'flick, touch or the next minute. Off: the snow is simulated ' +
          '20 times a second, all day.',
        defaultValue: true
      }
    ]
  },

  {
    type: 'submit',
    defaultValue: 'Save'
  }
];
