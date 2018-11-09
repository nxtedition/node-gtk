const gi = require('../../lib/')
const GLib = gi.require('GLib', '2.0')
const Gst = gi.require('Gst', '1.0')

gi.startLoop()

Gst.init()

const pipeline = new Gst.Pipeline("pipeline1")

const src1 = Gst.ElementFactory.make("videotestsrc", "src1")
const src2 = Gst.ElementFactory.make("videotestsrc", "src2")
const switcher = Gst.ElementFactory.make("input-selector", "switcher1")
const sink = Gst.ElementFactory.make("autovideosink", "sink1")

src1.pattern = 0
src2.pattern = 1

pipeline.add(src1)
pipeline.add(src2)
pipeline.add(switcher)
pipeline.add(sink)

src1.link(switcher)
src2.link(switcher)
switcher.link(sink)

pipeline.setState(Gst.State.PLAYING)

let flipflop = false
setInterval(() => {
    switcher.active_pad = flipflop ? switcher.getStaticPad('sink_0') : switcher.getStaticPad('sink_1')
    flipflop = !flipflop
}, 1000);

new GLib.MainLoop(null, false).run()
