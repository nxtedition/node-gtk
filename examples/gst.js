const gi = require('../lib/')
const GLib = gi.require('GLib', '2.0')
const Gst = gi.require('Gst', '1.0')

gi.startLoop()
const mainLoop = new GLib.MainLoop(null, false)

mainLoop.run()
