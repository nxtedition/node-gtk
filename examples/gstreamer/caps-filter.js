const gi = require('../../lib/')
const GLib = gi.require('GLib', '2.0')
const Gst = gi.require('Gst', '1.0')

gi.startLoop()

Gst.init()

let pipeline = new Gst.Pipeline("pipeline1")

let src = Gst.ElementFactory.make("videotestsrc", "src1")
let capsfilter = Gst.ElementFactory.make("capsfilter", "capsfilter1")
let sink = Gst.ElementFactory.make("autovideosink", "sink1")

capsfilter.caps = Gst.capsFromString("video/x-raw,width=640,height=480")

pipeline.add(src)
pipeline.add(capsfilter)
pipeline.add(sink)

src.link(capsfilter)
capsfilter.link(sink)

pipeline.setState(Gst.State.PLAYING)

new GLib.MainLoop(null, false).run()
