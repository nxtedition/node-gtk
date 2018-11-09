const gi = require('../lib/')
const GLib = gi.require('GLib', '2.0')
const Gst = gi.require('Gst', '1.0')

gi.startLoop()
const mainLoop = new GLib.MainLoop(null, false)

Gst.init()

const gstVersion = Gst.version()
console.log(`Gstreamer Version: ${gstVersion[0]}.${gstVersion[1]}.${gstVersion[2]}`)

const pipeline = new Gst.Pipeline("pipeline1")
pipeline.on('child-added', (element, name) => {
    console.log('child-added:', element, name)
})

const src = Gst.ElementFactory.make("videotestsrc", "src1")
const sink = Gst.ElementFactory.make("autovideosink", "sink1")

src.pattern = 1

pipeline.add(src)
pipeline.add(sink)
src.link(sink)

pipeline.setState(Gst.State.PLAYING)

mainLoop.run()
