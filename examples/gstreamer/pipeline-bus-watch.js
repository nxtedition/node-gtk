const gi = require('../../lib/')
const GLib = gi.require('GLib', '2.0')
const Gst = gi.require('Gst', '1.0')

gi.startLoop()

Gst.init()

const pipeline = new Gst.Pipeline("pipeline1")

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

let flipflop = false
setInterval(() => {
    pipeline.setState(flipflop ? Gst.State.PLAYING : Gst.State.PAUSED)
    flipflop = !flipflop
}, 1000);

new GLib.MainLoop(null, false).run()
