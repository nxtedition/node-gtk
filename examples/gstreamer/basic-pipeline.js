const gi = require('../../lib/')
const GLib = gi.require('GLib', '2.0')
const Gst = gi.require('Gst', '1.0')

gi.startLoop()

Gst.init()

const pipeline = new Gst.Pipeline("pipeline1")

const src = Gst.ElementFactory.make("videotestsrc", "src1")
const sink = Gst.ElementFactory.make("autovideosink", "sink1")

pipeline.add(src)
pipeline.add(sink)

src.link(sink)

pipeline.setState(Gst.State.PLAYING)

new GLib.MainLoop(null, false).run()
