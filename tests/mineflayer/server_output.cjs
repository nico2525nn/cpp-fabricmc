'use strict'

function observeServerOutput(child) {
  child.output = ''
  child.outputByStream = { stdout: '', stderr: '' }
  child.fixtureOutput = ''
  child.fixtureOutputByStream = { stdout: '', stderr: '' }
  child.fixtureEventsByStream = { stdout: [], stderr: [] }
  child.fixtureMarkerCountsByStream = {
    stdout: new Map(),
    stderr: new Map()
  }
  child.fixtureMarkersSeen = new Set()
  child.callbackThreadMismatch = false
  child.hasServerOutput = marker =>
    child.outputByStream.stdout.includes(marker) ||
    child.outputByStream.stderr.includes(marker)
  child.hasFixtureOutput = marker =>
    [...child.fixtureMarkersSeen].some(line => line.includes(marker))
  const callbackMarker = 'fixture CALLBACK_THREAD_MISMATCH'
  const streamState = new Map([
    ['stdout', { callbackMarkerTail: '', fixturePendingLine: '' }],
    ['stderr', { callbackMarkerTail: '', fixturePendingLine: '' }]
  ])
  const keepOutput = (streamName, data) => {
    const text = data.toString()
    const streamOutput = child.outputByStream[streamName] + text
    const state = streamState.get(streamName)
    const markerCandidate = state.callbackMarkerTail + text
    if (markerCandidate.includes(callbackMarker))
      child.callbackThreadMismatch = true
    state.callbackMarkerTail = markerCandidate.slice(-(callbackMarker.length - 1))
    child.outputByStream[streamName] = streamOutput.slice(-24000)
    child.output = ['stdout', 'stderr'].map(name =>
      `--- ${name} ---\n${child.outputByStream[name]}`).join('\n')
    const lines = (state.fixturePendingLine + text).split(/\r?\n/)
    state.fixturePendingLine = lines.pop() || ''
    for (const line of lines) {
      const markerStart = line.indexOf('fixture ')
      if (markerStart < 0) continue
      const completeFixtureLine = line.slice(markerStart).trim()
      const streamFixtureOutput = child.fixtureOutputByStream[streamName] +
        completeFixtureLine + '\n'
      child.fixtureOutputByStream[streamName] =
        streamFixtureOutput.slice(-8192)
      child.fixtureOutput = (child.fixtureOutput + line + '\n').slice(-8192)
      const marker = completeFixtureLine
      const streamEvents = child.fixtureEventsByStream[streamName]
      streamEvents.push(marker)
      if (streamEvents.length > 1024) streamEvents.shift()
      const streamCounts = child.fixtureMarkerCountsByStream[streamName]
      if (!streamCounts.has(marker) && streamCounts.size >= 4096)
        streamCounts.delete(streamCounts.keys().next().value)
      streamCounts.set(marker, (streamCounts.get(marker) || 0) + 1)
      if (!child.fixtureMarkersSeen.has(marker) &&
          child.fixtureMarkersSeen.size >= 4096)
        child.fixtureMarkersSeen.delete(child.fixtureMarkersSeen.values().next().value)
      child.fixtureMarkersSeen.add(marker)
    }
  }
  child.stdout.on('data', data => keepOutput('stdout', data))
  child.stderr.on('data', data => keepOutput('stderr', data))
  return child
}

module.exports = { observeServerOutput }
