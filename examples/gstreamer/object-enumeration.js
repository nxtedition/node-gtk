const gi = require('../../lib/')
const Gst = gi.require('Gst', '1.0')
Gst.init()
console.log(Gst.ElementFactory)