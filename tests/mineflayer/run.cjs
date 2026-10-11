'use strict'

const path = require('node:path')
const Module = require('node:module')

process.env.NODE_PATH = [
  path.join(__dirname, 'node_modules'),
  process.env.NODE_PATH
].filter(Boolean).join(path.delimiter)
Module._initPaths()

const smoke = require('../mineflayer_live_smoke.cjs')
if (process.argv.includes('--jvm-callback-only'))
  smoke.startJvmCallbackSmoke().catch(smoke.reportSmokeFailure)
else if (process.argv.includes('--entity-tracking-only'))
  smoke.startEntityTrackingDistanceSmoke().catch(smoke.reportSmokeFailure)
else
  smoke.startLiveSmoke().catch(smoke.reportSmokeFailure)
