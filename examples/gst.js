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

const bus = pipeline.getBus()
bus.addSignalWatch()
bus.on('message', (message) => {
    if (message.type == Gst.MessageType.STATE_CHANGED) {
        const states = message.parse_state_changed()
        console.log("State Changed: %d -> %d", states[0], states[1])
    }
})

const src = Gst.ElementFactory.make("videotestsrc", "src1")
const sink = Gst.ElementFactory.make("autovideosink", "sink1")

pipeline.add(src)
pipeline.add(sink)
src.link(sink)

pipeline.setState(Gst.State.PLAYING)

let pattern = true
setInterval(() => {
    src.pattern = pattern ? 1 : 0
    pattern = !pattern
}, 1000);

mainLoop.run()
