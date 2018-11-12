const gi = require('../../lib/')
const GLib = gi.require('GLib', '2.0')
const Gst = gi.require('Gst', '1.0')
const GstRtsp = gi.require('GstRtsp', '1.0')
const GstRtspServer = gi.require('GstRtspServer', '1.0')

gi.startLoop()

Gst.init()

const pipeline = new Gst.Pipeline('pipeline1')

const src1 = Gst.ElementFactory.make('videotestsrc', 'src1')
const src2 = Gst.ElementFactory.make('videotestsrc', 'src2')
const switcher = Gst.ElementFactory.make('input-selector', 'switcher1')
const capsfilter = Gst.ElementFactory.make('capsfilter', 'capsfilter1')
const sink = Gst.ElementFactory.make('intervideosink', 'sink1')

src1.pattern = 0
src2.pattern = 1
capsfilter.caps = Gst.capsFromString('video/x-raw,width=1280,height=720')
sink.channel = 'main'

pipeline.add(src1)
pipeline.add(src2)
pipeline.add(switcher)
pipeline.add(capsfilter)
pipeline.add(sink)

src1.link(switcher)
src2.link(switcher)
switcher.link(capsfilter)
capsfilter.link(sink)

pipeline.setState(Gst.State.PLAYING)

let flipflop = false
setInterval(() => {
    switcher.active_pad = flipflop ? switcher.getStaticPad('sink_0') : switcher.getStaticPad('sink_1')
    flipflop = !flipflop
}, 1000);

const rtspServer = new GstRtspServer.RTSPServer()
const rtspServerAddressPool = new GstRtspServer.RTSPAddressPool()
const rtspServerMediaFactory = new GstRtspServer.RTSPMediaFactory()

rtspServer.setService('554')
rtspServer.getSessionPool().setMaxSessions(3)
rtspServerAddressPool.addRange('0.0.0.0', '0.0.0.0', 30554, 30564, 0)
rtspServerMediaFactory.setAddressPool(rtspServerAddressPool)
rtspServerMediaFactory.setLaunch(`(
    intervideosrc channel=main
        ! queue
        ! videoscale
        ! videoconvert
        ! videorate
        ! video/x-raw,width=1280,height=720
        ! vp8enc deadline=100 target-bitrate=1145728
        ! rtpvp8pay name=pay0 pt=96
    audiotestsrc wave=ticks
        ! queue
        ! audioconvert
        ! opusenc
        ! rtpopuspay name=pay1 pt=97
)`)
rtspServerMediaFactory.setShared(true)
rtspServer.getMountPoints().addFactory('/preview', rtspServerMediaFactory);
rtspServer.on('client-connected', (client) => {

});
rtspServer.attach()

new GLib.MainLoop(null, false).run()
