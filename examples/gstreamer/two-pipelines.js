const gi = require('../../lib/')
const GLib = gi.require('GLib', '2.0')
const Gst = gi.require('Gst', '1.0')

gi.startLoop()

Gst.init()

const pipeline1 = new Gst.Pipeline("pipeline1")
const src1 = Gst.ElementFactory.make("videotestsrc", "src1")
const sink1 = Gst.ElementFactory.make("autovideosink", "sink1")
src1.pattern = 0
pipeline1.add(src1)
pipeline1.add(sink1)
src1.link(sink1)
pipeline1.setState(Gst.State.PLAYING)

const pipeline2 = new Gst.Pipeline("pipeline2")
const src2 = Gst.ElementFactory.make("videotestsrc", "src2")
const sink2 = Gst.ElementFactory.make("autovideosink", "sink2")
src2.pattern = 1
pipeline2.add(src2)
pipeline2.add(sink2)
src2.link(sink2)
pipeline2.setState(Gst.State.PLAYING)

new GLib.MainLoop(null, false).run()
