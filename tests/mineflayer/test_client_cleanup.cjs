'use strict'

const assert = require('node:assert/strict')
const { EventEmitter } = require('node:events')
const Module = require('node:module')
const path = require('node:path')
const { test } = require('node:test')

process.env.NODE_PATH = [
  path.join(__dirname, 'node_modules'),
  process.env.NODE_PATH
].filter(Boolean).join(path.delimiter)
Module._initPaths()

const {
  assertNoObservedBotErrors,
  connectBot,
  expectBotKick,
  expectJoinRejected,
  observeBotErrors,
  startManagedSmoke,
  stopBot,
  stopOwnedBots,
  stopOwnedServer
} = require('../mineflayer_live_smoke.cjs')

function fakeBot({ closed = false, autoCloseOnEnd = false } = {}) {
  const bot = new EventEmitter()
  bot.username = 'CleanupProbe'
  bot.quitCalls = 0
  bot.endEvents = 0
  bot.on('end', () => { bot.endEvents++ })

  const socket = new EventEmitter()
  socket.closed = closed
  socket.destroyed = closed
  socket.destroyCalls = 0
  socket.close = () => {
    if (socket.closed) return
    socket.closed = true
    socket.destroyed = true
    socket.emit('close', false)
  }
  socket.destroy = () => {
    socket.destroyCalls++
    socket.close()
  }

  const client = new EventEmitter()
  client.ended = closed
  client.endCalls = 0
  client.socket = socket
  client.end = () => {
    client.endCalls++
    if (!client.ended) {
      client.ended = true
      client.emit('end', 'fake-end')
      bot.emit('end', 'fake-end')
      if (autoCloseOnEnd) socket.close()
    }
  }
  client.on('error', error => bot.emit('error', error))
  bot._client = client
  bot.quit = () => {
    bot.quitCalls++
    client.end()
  }
  return bot
}

test('cleanup does not send quit after the socket has closed', async () => {
  const bot = fakeBot({ closed: true })

  await stopBot(bot)

  assert.equal(bot.quitCalls, 0)
  assert.equal(bot._client.endCalls, 0)
})

test('cleanup waits for socket close after the client end notification', async () => {
  const bot = fakeBot()
  let settled = false
  const stopping = stopBot(bot, {
    closeTimeoutMs: 250,
    forceCloseTimeoutMs: 250
  }).then(() => { settled = true })

  assert.equal(bot.quitCalls, 1)
  assert.equal(bot._client.endCalls, 1)
  assert.equal(bot.endEvents, 1)
  assert.equal(bot._client.socket.closed, false)

  await Promise.resolve()
  assert.equal(settled, false,
    'the client end event is not treated as terminal socket closure')

  bot._client.socket.close()
  await stopping
  assert.equal(bot._client.socket.closed, true)
  assert.equal(settled, true)
})

test('one bot cleanup failure does not skip other owned clients', async () => {
  const stuck = fakeBot()
  stuck._client.socket.destroy = () => {
    stuck._client.socket.destroyCalls++
  }
  const healthy = fakeBot({ autoCloseOnEnd: true })

  await assert.rejects(
    stopOwnedBots([stuck, healthy], {
      closeTimeoutMs: 5,
      forceCloseTimeoutMs: 5
    }),
    error => error instanceof AggregateError && error.errors.length === 1)
  assert.equal(stuck._client.socket.destroyCalls, 1,
    'the failed owner is force-closed before its bounded failure is reported')
  assert.equal(healthy._client.socket.closed, true,
    'another bot still reaches terminal socket close')
})

test('failed login cleanup remains reachable for the outer owner', async () => {
  const bot = fakeBot()
  let destroyCalls = 0
  bot._client.socket.destroy = () => {
    destroyCalls++
    if (destroyCalls > 1) bot._client.socket.close()
  }

  await assert.rejects(connectBot('UnspawnedProbe', 25565, null, {
    createBot: () => bot,
    spawnTimeoutMs: 5,
    closeTimeoutMs: 5,
    forceCloseTimeoutMs: 5
  }), error => error instanceof AggregateError && error.errors.length === 2)
  assert.equal(destroyCalls, 1,
    'connect failure reports the first bounded socket cleanup failure')
  await assert.rejects(
    startManagedSmoke(() => assert.fail('must not run with an unclosed client'),
                      1000, 'retry with unclosed client'),
    /owned clients remain/)

  await stopOwnedBots([], { closeTimeoutMs: 5, forceCloseTimeoutMs: 5 })
  assert.equal(destroyCalls, 2,
    'the outer owner can retry the still-owned bot after connectBot rejects')
  assert.equal(bot._client.socket.closed, true)
})

test('managed smoke preserves rejection for an awaiting caller', async () => {
  const failure = new Error('injected smoke failure')
  await assert.rejects(
    startManagedSmoke(() => Promise.reject(failure), 1000, 'unit smoke'),
    error => error === failure)
})

test('unexpected gameplay kick fails the managed smoke', async () => {
  const bot = fakeBot()
  await assert.rejects(startManagedSmoke(async () => {
    observeBotErrors(bot)
    bot.emit('kicked', { text: 'unexpected gameplay kick' })
    return 'scenario completed'
  }, 1000, 'unexpected kick probe'), /was unexpectedly kicked/)
})

test('only a matching explicitly expected kick is accepted by a smoke', async () => {
  const expected = new EventEmitter()
  expected.username = 'ExpectedKickProbe'
  const unexpected = new EventEmitter()
  unexpected.username = 'UnexpectedKickProbe'

  await assert.rejects(startManagedSmoke(() => {
    observeBotErrors(expected)
    observeBotErrors(unexpected)
    expectBotKick(expected, 'expected shutdown')
    expected.emit('kicked', { text: 'expected shutdown requested' })
    unexpected.emit('kicked', { text: 'different reason' })
  }, 1000, 'expected-kick classification'),
  /UnexpectedKickProbe was unexpectedly kicked/)

  await startManagedSmoke(() => {
    const bot = new EventEmitter()
    bot.username = 'ExpectedOnlyProbe'
    observeBotErrors(bot)
    expectBotKick(bot, 'expected shutdown')
    bot.emit('kicked', { text: 'expected shutdown requested' })
  }, 1000, 'matching expected-kick classification')
})

test('a concurrent smoke is rejected without mutating the active run', async () => {
  const bot = fakeBot()
  let releaseActiveRun
  const activeRunGate = new Promise(resolve => { releaseActiveRun = resolve })
  const activeRun = startManagedSmoke(async () => {
    observeBotErrors(bot)
    await activeRunGate
    return 'active run completed'
  }, 1000, 'active concurrency probe')
  await Promise.resolve()
  bot.emit('kicked', { text: 'active run kick' })

  let competingOperationRan = false
  await assert.rejects(startManagedSmoke(() => {
    competingOperationRan = true
  }, 1000, 'competing concurrency probe'), /already in progress/)
  releaseActiveRun()
  await assert.rejects(activeRun, /was unexpectedly kicked/)
  assert.equal(competingOperationRan, false,
    'the rejected invocation never starts or clears active-run state')
})

test('a later smoke run is blocked while an owned server is unreaped', async () => {
  const child = new EventEmitter()
  child.pid = 1234
  child.exitCode = null
  child.signalCode = null
  child.kill = () => true

  await assert.rejects(stopOwnedServer(child, false, {
    gracefulTimeoutMs: 5,
    forceTimeoutMs: 5
  }), /could not stop owned cppfm process/)
  await assert.rejects(
    startManagedSmoke(() => assert.fail('must not run with an unreaped server'),
                      1000, 'retry with unreaped server'),
    /owned cppfm processes remain/)

  child.signalCode = 'SIGKILL'
  child.emit('exit', null, 'SIGKILL')
  assert.equal(await startManagedSmoke(() => 'retry is now safe',
                                       1000, 'retry after child exit'),
               'retry is now safe')
})

test('a post-spawn kill error does not release server ownership', async () => {
  const child = new EventEmitter()
  child.pid = 1235
  child.exitCode = null
  child.signalCode = null
  child.spawnError = null
  // Model Node's ChildProcess error hook from before it distinguished spawn
  // failures from kill errors. A live pid must remain owned either way.
  child.once('error', error => { child.spawnError = error })
  child.kill = signal => {
    if (signal === 'SIGTERM') child.emit('error', new Error('injected EPERM'))
    return true
  }

  await assert.rejects(stopOwnedServer(child, false, {
    gracefulTimeoutMs: 5,
    forceTimeoutMs: 5
  }), /could not stop owned cppfm process/)
  assert.ok(child.spawnError,
    'the injected post-spawn error sets the historical ambiguous error field')
  await assert.rejects(
    startManagedSmoke(() => assert.fail('must not run with a live child'),
                      1000, 'retry after kill error'),
    /owned cppfm processes remain/)

  child.signalCode = 'SIGKILL'
  child.emit('exit', null, 'SIGKILL')
})

test('a kill error does not complete shutdown before the child exits', async () => {
  const child = new EventEmitter()
  child.pid = 1236
  child.exitCode = null
  child.signalCode = null
  child.kill = signal => {
    assert.equal(signal, 'SIGTERM')
    child.emit('error', new Error('injected transient kill error'))
    setTimeout(() => {
      child.exitCode = 0
      child.emit('exit', 0, null)
    }, 15)
    return true
  }

  await stopOwnedServer(child, true, {
    gracefulTimeoutMs: 100,
    forceTimeoutMs: 50
  })
  assert.equal(child.exitCode, 0,
    'shutdown awaits the real exit event after a ChildProcess error')
})

test('owned-server shutdown does not leave the losing timeout armed', async () => {
  const child = new EventEmitter()
  child.pid = 1234
  child.exitCode = null
  child.signalCode = null
  child.kill = signal => {
    assert.equal(signal, 'SIGTERM')
    child.exitCode = 0
    child.emit('exit', 0, null)
    return true
  }

  const originalSetTimeout = global.setTimeout
  const originalClearTimeout = global.clearTimeout
  const timeoutHandles = new Set()
  const clearedTimeoutHandles = new Set()
  global.setTimeout = (callback, delay, ...args) => {
    const handle = originalSetTimeout(callback, delay, ...args)
    if (delay === 10000) timeoutHandles.add(handle)
    return handle
  }
  global.clearTimeout = handle => {
    if (timeoutHandles.has(handle)) clearedTimeoutHandles.add(handle)
    return originalClearTimeout(handle)
  }
  try {
    await stopOwnedServer(child, true)
  } finally {
    global.setTimeout = originalSetTimeout
    global.clearTimeout = originalClearTimeout
  }
  assert.equal(child.exitCode, 0)
  assert.equal(timeoutHandles.size, 1,
    'clean shutdown creates one bounded losing-race timer')
  assert.deepEqual(clearedTimeoutHandles, timeoutHandles,
    'the losing shutdown timeout is canceled when exit wins')
})

test('a non-kicked probe error rejects instead of counting as a join kick', async () => {
  const bot = fakeBot({ autoCloseOnEnd: true })
  const probeError = new Error('probe connection failed before kick')
  const pending = expectJoinRejected('RejectedProbe', 25565, {
    createBot: () => bot,
    timeoutMs: 100,
    closeTimeoutMs: 100,
    forceCloseTimeoutMs: 100
  })

  queueMicrotask(() => bot.emit('error', probeError))

  await assert.rejects(pending, error => error === probeError)
  assert.equal(bot._client.socket.closed, true,
    'the failed probe still waits for and completes owned socket cleanup')
  assert.doesNotThrow(assertNoObservedBotErrors,
    'probe-local errors do not leak into the final gameplay-client assertion')
})

test('a kicked probe absorbs a late socket error through terminal close', async () => {
  const bot = fakeBot()
  const reason = { text: 'expected server rejection' }
  const pending = expectJoinRejected('KickedProbe', 25565, {
    createBot: () => bot,
    timeoutMs: 100,
    closeTimeoutMs: 250,
    forceCloseTimeoutMs: 250
  })

  const clientEnd = new Promise(resolve => bot.once('end', resolve))
  bot.emit('kicked', reason)
  await clientEnd
  assert.equal(bot._client.socket.closed, false)
  assert.equal(bot._client.endCalls, 1,
    'cleanup ends the kicked connection without sending another quit packet')

  bot._client.emit('error', new Error('late EPIPE after kick packet'))
  assert.doesNotThrow(assertNoObservedBotErrors,
    'expected kick-probe socket errors are excluded from the final assertion')
  bot._client.socket.close()

  assert.equal(await pending, reason)
  assert.doesNotThrow(assertNoObservedBotErrors)
})
