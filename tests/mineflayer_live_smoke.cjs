#!/usr/bin/env node
'use strict'

// Opt-in black-box play probe for a local 1.21.4 cppfm server. Mineflayer is
// deliberately isolated in tests/mineflayer rather than a build-time
// requirement of the C++ project. This harness owns an isolated throwaway
// server directory and process; it never attaches to an arbitrary server.
const mineflayer = require('mineflayer')
const mineflayerVersion = require('mineflayer/package.json').version
const minecraftDataVersion = require('minecraft-data/package.json').version
const nbt = require('prismarine-nbt')
const net = require('node:net')
const { once } = require('node:events')
const { spawn } = require('node:child_process')
const { createHash, randomBytes } = require('node:crypto')
const fs = require('node:fs/promises')
const os = require('node:os')
const path = require('node:path')
const { Vec3 } = require('vec3')
const { observeServerOutput } = require('./mineflayer/server_output.cjs')

const repoRoot = path.resolve(__dirname, '..')
const binaryName = process.platform === 'win32' ? 'cppfm.exe' : 'cppfm'
const binaryPath = path.resolve(
  repoRoot,
  process.env.CPPFM_BINARY || path.join('build', binaryName))
const controller = new AbortController()
const signal = controller.signal
let receivedSignal = null
const signalHandlers = new Map()

function assert(condition, message) {
  if (!condition) throw new Error(`FAIL: ${message}`)
  console.log(`PASS: ${message}`)
}

function fixtureMarkerCount(child, marker) {
  return ['stdout', 'stderr'].reduce((count, streamName) =>
    count + (child.fixtureMarkerCountsByStream[streamName].get(marker) || 0), 0)
}

function fixtureEventCount(child, prefix) {
  return ['stdout', 'stderr'].reduce((count, streamName) =>
    count + child.fixtureEventsByStream[streamName].reduce((matches, event) =>
      matches + (event.startsWith(prefix) ? 1 : 0), 0), 0)
}

function lastFixtureEvent(child, prefix) {
  // NativeAccess fixture logging uses stderr. Keep each stream's ordering
  // separate; event arrival order between OS pipes is not evidence order.
  for (const streamName of ['stderr', 'stdout']) {
    const events = child.fixtureEventsByStream[streamName]
    for (let i = events.length - 1; i >= 0; i--) {
      if (events[i].startsWith(prefix)) return events[i]
    }
  }
  return ''
}

function fixtureSequence(child, markers) {
  for (const streamName of ['stderr', 'stdout']) {
    const events = child.fixtureEventsByStream[streamName]
    let cursor = 0
    const positions = []
    for (const marker of markers) {
      const position = events.findIndex((event, index) =>
        index >= cursor && event.includes(marker))
      if (position < 0) break
      positions.push(position)
      cursor = position + 1
    }
    if (positions.length === markers.length)
      return { streamName, positions, events }
  }
  return null
}

function entityIdFromFixtureEvent(event) {
  const match = /^fixture ENTITY_LOAD_ID id=(\d+) /.exec(event)
  if (!match) throw new Error(`invalid entity-load fixture event: ${event}`)
  return Number(match[1])
}

function withAbort(promise, abortSignal = signal) {
  if (abortSignal.aborted)
    return Promise.reject(abortSignal.reason || new Error('operation aborted'))
  return new Promise((resolve, reject) => {
    const onAbort = () => {
      cleanup()
      reject(abortSignal.reason || new Error('operation aborted'))
    }
    const cleanup = () => abortSignal.removeEventListener('abort', onAbort)
    abortSignal.addEventListener('abort', onAbort, { once: true })
    Promise.resolve(promise).then(
      value => { cleanup(); resolve(value) },
      error => { cleanup(); reject(error) })
  })
}

function withTimeout(promise, timeoutMs, description) {
  let timer
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new Error(`timed out waiting for ${description}`)),
                       timeoutMs)
  })
  return withAbort(Promise.race([promise, timeout])).finally(() => clearTimeout(timer))
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms))
}

function countItems(items, name) {
  return items.filter(item => item.name === name)
    .reduce((total, item) => total + item.count, 0)
}

function countPlayerItems(bot, name) {
  // prismarine-windows' inventory.items() covers slots 9..44; the protocol's
  // offhand slot 45 is deliberately outside that range.
  const offhand = bot.inventory.slots[45]
  return countItems(bot.inventory.items(), name) +
    (offhand?.name === name ? offhand.count : 0)
}

function enabledDatapackOrder(response) {
  const match = response.match(/(?:^|\n)Enabled \(\d+\): ([^\n]*)/)
  return match ? match[1].trim().split(/\s+/).filter(Boolean) : []
}

function offlinePlayerUuidHex(username) {
  const uuid = createHash('md5')
    .update(`OfflinePlayer:${username}`, 'utf8')
    .digest()
  uuid[6] = (uuid[6] & 0x0f) | 0x30
  uuid[8] = (uuid[8] & 0x3f) | 0x80
  return uuid.toString('hex')
}

function countItemsInWindowInventory(window, name) {
  return countItems(
    window.slots.slice(window.inventoryStart, window.inventoryEnd)
      .filter(Boolean),
    name)
}

function signTextLinesFromNbt(entity) {
  const root = entity?.value ?? entity
  const front = root?.front_text?.value ?? root?.front_text
  const messageList = front?.messages?.value ?? front?.messages
  const messages = Array.isArray(messageList)
    ? messageList
    : messageList?.value
  if (!Array.isArray(messages)) return null
  return messages.map(entry => {
    const raw = entry?.value ?? entry
    if (typeof raw !== 'string') return null
    try {
      const component = JSON.parse(raw)
      if (typeof component === 'string') return component
      if (component && typeof component.text === 'string')
        return component.text
    } catch (_) {}
    return raw
  })
}

function persistenceLogSummary(output) {
  return output.split(/\r?\n/).filter(line =>
    /saved r\.-?\d+\.-?\d+ mca|flushChunk|FLUSH CHUNK|stored chunk|validated chunk|discarded stale|could not queue chunk|async chunk|chunk extras|level source|region transaction/i.test(line)
  ).slice(-100).join('\n')
}

async function waitUntil(predicate, description, timeoutMs = 10000) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    if (signal.aborted) throw signal.reason || new Error('operation aborted')
    const value = predicate()
    if (value) return value
    await withAbort(sleep(100))
  }
  throw new Error(`timed out waiting for ${description}`)
}

async function waitForRconBlockState(rcon, position, expected,
                                     description, timeoutMs = 15000) {
  const deadline = Date.now() + timeoutMs
  let lastResult = ''
  while (Date.now() < deadline) {
    lastResult = await rcon.command(
      `data get block ${position.x} ${position.y} ${position.z}`)
    if (lastResult.includes(`${expected} state=`)) return lastResult
    await withAbort(sleep(100))
  }
  throw new Error(`timed out waiting for ${description}; ` +
                  `last server state=${lastResult}`)
}

async function waitForRconPlayerAbsent(rcon, username, timeoutMs = 10000) {
  const deadline = Date.now() + timeoutMs
  let lastResult = ''
  while (Date.now() < deadline) {
    lastResult = await rcon.command('list')
    if (!lastResult.includes(username)) return lastResult
    await withAbort(sleep(100))
  }
  throw new Error(`server did not finish disconnecting ${username}; ` +
                  `last player list=${lastResult}`)
}

async function clickSlotAndWaitForState(bot, window, slot, mouseButton,
                                        stateIds) {
  const stateId = stateIds.get(window.id)
  if (!Number.isInteger(stateId))
    throw new Error(`no current state id for container ${window.id}`)

  const response = new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      bot._client.removeListener('window_items', onItems)
      reject(new Error(`no authoritative update for container ${window.id}`))
    }, 5000)
    const onItems = packet => {
      if (packet.windowId !== window.id) return
      clearTimeout(timer)
      bot._client.removeListener('window_items', onItems)
      resolve(packet)
    }
    bot._client.on('window_items', onItems)
  })
  bot._client.write('window_click', {
    windowId: window.id,
    stateId,
    slot,
    mouseButton,
    mode: 0,
    changedSlots: [],
    cursorItem: { itemCount: 0, components: [], removeComponents: [] }
  })
  const packet = await withAbort(response)
  stateIds.set(window.id, packet.stateId)
  return packet
}

async function nextWindowItems(bot, windowIdOrPredicate, description) {
  return withTimeout(new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      bot._client.removeListener('window_items', onItems)
      reject(new Error(`no authoritative items packet for ${description}`))
    }, 5000)
    const onItems = packet => {
      const matches = typeof windowIdOrPredicate === 'function'
        ? windowIdOrPredicate(packet)
        : packet.windowId === windowIdOrPredicate
      if (!matches) return
      clearTimeout(timer)
      bot._client.removeListener('window_items', onItems)
      resolve(packet)
    }
    bot._client.on('window_items', onItems)
  }), 6000, description)
}

async function depositOneWithAuthoritativeClicks(bot, window, itemId,
                                                  stateIds) {
  const source = window.findItemRange(
    window.inventoryStart, window.inventoryEnd, itemId, null, false)
  if (!source) throw new Error(`item ${itemId} not found in player inventory`)
  await clickSlotAndWaitForState(bot, window, source.slot, 0, stateIds)
  const emptyContainerSlot = window.firstEmptySlotRange(0, window.inventoryStart)
  if (emptyContainerSlot === null)
    throw new Error('container has no empty slot for the test item')
  await clickSlotAndWaitForState(bot, window, emptyContainerSlot, 1, stateIds)
  await clickSlotAndWaitForState(bot, window, source.slot, 0, stateIds)
}

async function reserveLoopbackPorts() {
  const servers = []
  try {
    for (let i = 0; i < 2; i++) {
      const server = net.createServer()
      await new Promise((resolve, reject) => {
        server.once('error', reject)
        server.listen(0, '127.0.0.1', resolve)
      })
      servers.push(server)
    }
    const ports = servers.map(server => server.address().port)
    await Promise.all(servers.map(server => new Promise((resolve, reject) =>
      server.close(error => error ? reject(error) : resolve()))))
    return ports
  } catch (error) {
    await Promise.all(servers.map(server => new Promise(resolve =>
      server.close(() => resolve()))))
    throw error
  }
}

function canConnect(port) {
  return new Promise((resolve, reject) => {
    const socket = net.createConnection({ host: '127.0.0.1', port })
    const onAbort = () => {
      socket.destroy()
      reject(signal.reason || new Error('operation aborted'))
    }
    signal.addEventListener('abort', onAbort, { once: true })
    const done = value => {
      signal.removeEventListener('abort', onAbort)
      socket.destroy()
      resolve(value)
    }
    socket.once('connect', () => done(true))
    socket.once('error', () => done(false))
  })
}

async function waitForPort(child, port, description, timeoutMs = 30000) {
  const deadline = Date.now() + timeoutMs
  while (Date.now() < deadline) {
    if (signal.aborted) throw signal.reason || new Error('operation aborted')
    if (child.spawnError)
      throw new Error(`could not launch cppfm: ${child.spawnError.message}`)
    if (child.exitCode !== null || child.signalCode !== null)
      throw new Error(`cppfm exited before ${description} was ready\n${child.output}`)
    if (await withAbort(canConnect(port))) return
    await withAbort(sleep(100))
  }
  throw new Error(`timed out waiting for cppfm ${description}\n${child.output}`)
}

function startOwnedServer(binary, root, gamePort, jvmFixture = false) {
  const args = [
    `--port=${gamePort}`,
    '--world-dir=world',
    // This probe measures player protocol behavior on stable terrain, not the
    // cost of normal-world structure generation.
    '--level-type=flat',
    '--online-mode=false',
    '--enforce-secure-profile=false',
    '--view-distance=4',
    '--simulation-distance=4',
    '--enable-rcon=true'
  ]
  if (jvmFixture) {
    const buildRoot = path.dirname(binary)
    args.push('--jvm=true', '--jvm-strict=true',
      `--jvm-classes=${path.join(buildRoot, 'jvm', 'classes')}`,
      `--jvm-mods=${path.join(buildRoot, 'jvm', 'fixture-mods')}`)
  } else {
    args.push('--jvm=false')
  }
  const child = spawn(binary, args, {
    cwd: repoRoot,
    env: { ...process.env, CPPFM_SERVER_DIR: root },
    stdio: ['ignore', 'pipe', 'pipe']
  })
  child.spawnError = null
  observeServerOutput(child)
  child.once('error', error => { child.spawnError = error })
  return child
}

class RconClient {
  constructor(socket) {
    this.socket = socket
    this.buffer = Buffer.alloc(0)
    this.waiters = new Map()
    this.nextId = 1
    // The live fixture includes cold startup, embedded JVM callbacks, and
    // synchronous server-thread re-entry.  Five seconds intermittently
    // expires while the first real-player command is still being processed;
    // keep the failure bounded but allow a slow local tick to return.
    this.responseTimeoutMs = 15000
    socket.on('data', data => {
      this.buffer = Buffer.concat([this.buffer, data])
      while (this.buffer.length >= 4) {
        const length = this.buffer.readInt32LE(0)
        if (length < 10 || length > 4110) {
          this.fail(new Error(`invalid RCON frame length ${length}`))
          return
        }
        if (this.buffer.length < 4 + length) return
        const frame = this.buffer.subarray(4, 4 + length)
        this.buffer = this.buffer.subarray(4 + length)
        const packet = {
          id: frame.readInt32LE(0),
          type: frame.readInt32LE(4),
          body: frame.subarray(8, length - 2).toString('utf8')
        }
        // RCON responses carry the request id. Never let a late response from
        // a timed-out command satisfy a later command's FIFO waiter.
        const waiter = this.waiters.get(packet.id) ||
          (packet.id === -1 ? this.waiters.get(1) : null)
        if (waiter) {
          this.waiters.delete(waiter.id)
          waiter.resolve(packet)
        }
      }
    })
    socket.on('error', error => this.fail(error))
    socket.on('close', () => this.fail(new Error('RCON socket closed')))
  }

  fail(error) {
    for (const waiter of this.waiters.values()) waiter.reject(error)
    this.waiters.clear()
  }

  readPacket(id, timeoutMs = this.responseTimeoutMs) {
    let waiter
    const response = new Promise((resolve, reject) => {
      waiter = { id, resolve, reject }
      this.waiters.set(id, waiter)
    })
    return withTimeout(response, timeoutMs, `RCON response ${id}`).finally(() => {
      if (this.waiters.get(id) === waiter) this.waiters.delete(id)
    })
  }

  async requestPacket(id, type, payload) {
    const response = this.readPacket(id)
    try {
      this.sendPacket(id, type, payload)
    } catch (error) {
      const waiter = this.waiters.get(id)
      if (waiter) {
        this.waiters.delete(id)
        waiter.reject(error)
      }
    }
    return response
  }

  sendPacket(id, type, payload) {
    const text = Buffer.from(payload, 'utf8')
    const frame = Buffer.alloc(4 + 8 + text.length + 2)
    frame.writeInt32LE(10 + text.length, 0)
    frame.writeInt32LE(id, 4)
    frame.writeInt32LE(type, 8)
    text.copy(frame, 12)
    frame[frame.length - 2] = 0
    frame[frame.length - 1] = 0
    this.socket.write(frame)
  }

  async authenticate(password) {
    const response = await this.requestPacket(1, 3, password)
    if (response.id !== 1 || response.type !== 2)
      throw new Error('RCON authentication failed')
  }

  async command(command) {
    const id = ++this.nextId
    const response = await this.requestPacket(id, 2, command)
    if (response.id !== id || response.type !== 2)
      throw new Error(`unexpected RCON response for ${command}`)
    const noOpFill = /^No blocks were filled$/i.test(response.body.trim())
    if (!noOpFill &&
        /unknown(?: or incomplete)? command|incorrect argument|no block|failed|error:/i.test(response.body))
      throw new Error(`RCON command failed (${command}): ${response.body}`)
    return response.body
  }

  close() {
    this.socket.end()
    this.socket.destroy()
  }
}

async function connectRcon(port, password) {
  const socket = net.createConnection({ host: '127.0.0.1', port })
  try {
    await withTimeout(once(socket, 'connect'), 5000, 'RCON connection')
    const client = new RconClient(socket)
    await client.authenticate(password)
    return client
  } catch (error) {
    socket.destroy()
    throw error
  }
}

async function waitForRcon(port, password) {
  const deadline = Date.now() + 15000
  let lastError
  while (Date.now() < deadline) {
    if (signal.aborted) throw signal.reason || new Error('operation aborted')
    try {
      return await connectRcon(port, password)
    } catch (error) {
      lastError = error
      await withAbort(sleep(100))
    }
  }
  throw new Error(`RCON did not become ready: ${lastError?.message || 'timeout'}`)
}

async function startReadyProbeServer(binary, root, rconPassword,
                                     propertyOverrides = '',
                                     onChildStarted = null) {
  const maxAttempts = 3
  for (let attempt = 1; attempt <= maxAttempts; attempt++) {
    const [gamePort, rconPort] = await reserveLoopbackPorts()
    await fs.writeFile(path.join(root, 'server.properties'),
      `rcon.port=${rconPort}\nrcon.password=${rconPassword}\n` +
      propertyOverrides)
    const child = startOwnedServer(binary, root, gamePort)
    if (onChildStarted) onChildStarted(child)
    try {
      await waitForPort(child, gamePort, 'Minecraft port')
      await waitForPort(child, rconPort, 'RCON port')
      const rcon = await waitForRcon(rconPort, rconPassword)
      return { child, gamePort, rconPort, rcon }
    } catch (error) {
    const bindConflict = ['stdout', 'stderr'].some(streamName => {
      const output = child.outputByStream[streamName]
      return /bind\(\) failed:.*(?:address already in use|EADDRINUSE|only one usage)/i
        .test(output) || /RCON not started/i.test(output)
    })
      await stopOwnedServer(child)
      if (!bindConflict || attempt === maxAttempts) throw error
      console.warn(`loopback port allocation raced; retry ${attempt + 1}/${maxAttempts}`)
    }
  }
  throw new Error('could not reserve free loopback ports for cppfm')
}

async function connectBot(username, port, onClientCreated = null) {
  const bot = mineflayer.createBot({
    host: '127.0.0.1',
    port,
    username,
    version: '1.21.4',
    auth: 'offline'
  })
  if (onClientCreated) onClientCreated(bot)
  try {
    await withTimeout(once(bot, 'spawn'), 60000,
                      `${username} spawn`)
    // Mineflayer emits spawn before the first terrain columns necessarily
    // arrive. Wait for the authoritative spawn column before using physics.
    await waitUntil(() => bot.entity && bot.blockAt(bot.entity.position) !== null,
                    `${username} spawn chunk`, 30000)
    return bot
  } catch (error) {
    try { bot.quit('probe setup failed') } catch (_) {}
    try { bot._client?.end() } catch (_) {}
    throw error
  }
}

async function expectJoinRejected(username, port) {
  const bot = mineflayer.createBot({
    host: '127.0.0.1', port, username, version: '1.21.4', auth: 'offline',
    logErrors: false, hideErrors: true
  })
  let wasKicked = false
  const absorbLateSocketError = () => {}
  bot.on('error', absorbLateSocketError)
  bot._client?.on('error', absorbLateSocketError)
  try {
    const result = await withTimeout(new Promise((resolve, reject) => {
      const cleanup = () => {
        bot.removeListener('kicked', onKicked)
        bot.removeListener('spawn', onSpawn)
        bot.removeListener('error', onError)
      }
      const onKicked = reason => {
        wasKicked = true
        cleanup()
        resolve({ kicked: reason })
      }
      const onSpawn = () => { cleanup(); resolve({ spawned: true }) }
      const onError = error => { cleanup(); reject(error) }
      bot.once('kicked', onKicked)
      bot.once('spawn', onSpawn)
      bot.once('error', onError)
    }), 15000, `${username} join rejection`)
    if (!Object.prototype.hasOwnProperty.call(result, 'kicked'))
      throw new Error(`${username} reached play despite its Fabric join callback disconnect`)
    return result.kicked
  } finally {
    if (wasKicked) {
      if (!bot._client?.ended) bot._client?.end()
    } else {
      await stopBot(bot)
    }
    // Keep the local error sinks attached: the protocol framer can report a
    // delayed EPIPE after the server has already delivered the kick packet.
  }
}

async function waitForChat(bot, expectedUser, marker) {
  return withTimeout(new Promise((resolve, reject) => {
    function onChat(username, message) {
      if (username !== expectedUser || !String(message).includes(marker)) return
      cleanup()
      resolve(String(message))
    }
    const timer = setTimeout(() => {
      cleanup()
      reject(new Error(`chat marker ${marker} was not broadcast`))
    }, 10000)
    const cleanup = () => {
      clearTimeout(timer)
      bot.removeListener('chat', onChat)
    }
    bot.on('chat', onChat)
  }), 11000, 'cross-client chat')
}

async function stopBot(bot) {
  if (!bot) return
  let ended
  try { ended = once(bot, 'end').catch(() => {}) } catch (_) {}
  try { bot.quit('probe complete') } catch (_) {}
  if (ended) await Promise.race([ended, sleep(1000)])
  try { bot._client?.end() } catch (_) {}
}

async function stopOwnedServer(child, requireGraceful = false) {
  if (!child || child.pid == null) return
  if (child.exitCode !== null || child.signalCode !== null) {
    if (requireGraceful &&
        (child.signalCode !== null || child.exitCode !== 0))
      throw new Error(`cppfm exited uncleanly (code=${child.exitCode}, ` +
                      `signal=${child.signalCode})`)
    return
  }
  const exited = once(child, 'exit').catch(() => {})
  let requiredForceKill = false
  try { child.kill('SIGTERM') } catch (_) {}
  // World and region flushes must finish before this harness reuses the same
  // isolated world directory for its unloaded-chunk recovery scenario.
  await Promise.race([exited, sleep(10000)])
  if (child.exitCode === null && child.signalCode === null) {
    requiredForceKill = true
    try { child.kill('SIGKILL') } catch (_) { try { child.kill() } catch (_) {} }
    await Promise.race([exited, sleep(5000)])
  }
  if (child.exitCode === null && child.signalCode === null)
    throw new Error(`could not stop owned cppfm process pid=${child.pid}`)
  if (requireGraceful &&
      (requiredForceKill || child.signalCode !== null || child.exitCode !== 0))
    throw new Error(`cppfm did not stop cleanly (forced=${requiredForceKill}, ` +
                    `code=${child.exitCode}, signal=${child.signalCode})`)
}

async function main() {
  let root
  let child
  let rcon
  let first
  let second
  let persistedInventoryProbe
  let unsupportedEnderProbe
  let container
  let observerContainer
  let previousServerOutput = ''
  let primaryError = null
  try {
    console.log(`Mineflayer ${mineflayerVersion}; minecraft-data ${minecraftDataVersion}; level-type=flat`)
    if (!binaryPath) throw new Error('CPPFM_BINARY is required')
    await fs.access(binaryPath)
    root = await fs.mkdtemp(path.join(os.tmpdir(), 'cppfm-mineflayer-'))
    const probeLootDirectory = path.join(
      root, 'world', 'datapacks', 'mineflayer_probe', 'data',
      'minecraft', 'loot_table', 'blocks')
    const probeChestLootDirectory = path.join(
      root, 'world', 'datapacks', 'mineflayer_probe', 'data',
      'minecraft', 'loot_table', 'chests')
    const probeRecipeDirectory = path.join(
      root, 'world', 'datapacks', 'mineflayer_probe', 'data',
      'mineflayer_probe', 'recipe')
    await Promise.all([
      fs.mkdir(probeLootDirectory, { recursive: true }),
      fs.mkdir(probeChestLootDirectory, { recursive: true }),
      fs.mkdir(probeRecipeDirectory, { recursive: true })
    ])
    await fs.writeFile(path.join(
      root, 'world', 'datapacks', 'mineflayer_probe', 'pack.mcmeta'),
    JSON.stringify({
      pack: {
        pack_format: 61,
        description: 'cppfm Mineflayer 1.21.4 gameplay fixture'
      }
    }, null, 2))
    await fs.writeFile(path.join(probeLootDirectory, 'dirt.json'),
      JSON.stringify({
        pools: [{
          rolls: 1,
          entries: [{ type: 'minecraft:item', name: 'minecraft:gold_nugget' }]
        }]
      }))
    await fs.writeFile(path.join(probeLootDirectory, 'empty.json'),
      JSON.stringify({
        pools: [{
          rolls: { min: 0, max: 0 },
          entries: [{ type: 'minecraft:item', name: 'minecraft:diamond' }]
        }]
      }))
    await fs.writeFile(path.join(probeLootDirectory, 'stone.json'),
      JSON.stringify({
        pools: [{
          rolls: { min: 0, max: 0 },
          entries: [{ type: 'minecraft:item', name: 'minecraft:diamond' }]
        }]
      }))
    await fs.writeFile(path.join(probeLootDirectory, 'cobblestone.json'),
      JSON.stringify({
        pools: [{
          rolls: 1,
          entries: [{
            type: 'minecraft:item',
            name: 'minecraft:diamond',
            conditions: [{ condition: 'minecraft:unknown_condition' }]
          }]
        }]
      }))
    await fs.writeFile(
      path.join(probeChestLootDirectory, 'simple_dungeon.json'),
      JSON.stringify({
        pools: [{
          rolls: 1,
          entries: [{ type: 'minecraft:item', name: 'minecraft:diamond' }]
        }]
      }))
    await fs.writeFile(
      path.join(probeRecipeDirectory, 'diamond_horse_armor.json'),
      JSON.stringify({
        type: 'minecraft:crafting_shapeless',
        ingredients: [{ item: 'minecraft:lapis_lazuli' }],
        result: { id: 'minecraft:diamond_horse_armor', count: 1 }
      }))
    const rconPassword = randomBytes(24).toString('hex')
    const ready = await startReadyProbeServer(
      binaryPath, root, rconPassword, '', owned => { child = owned })
    child = ready.child
    const { gamePort } = ready
    rcon = ready.rcon

    let firstInventorySyncObserved = false
    let firstInventoryRevision = 0
    const recipeBookEntries = []
    let recipeBookRevision = 0
    const itemDropTrace = []
    const rangedProjectileSpawns = []
    first = await connectBot('CppfmProbeA', gamePort, bot => {
      bot._client.on('window_items', packet => {
        if (packet.windowId === 0) {
          firstInventorySyncObserved = true
          firstInventoryRevision++
        }
      })
      bot._client.on('recipe_book_add', packet => {
        if (packet.replace) recipeBookEntries.length = 0
        recipeBookEntries.push(...(packet.entries || []))
        recipeBookRevision++
      })
      bot.on('entitySpawn', entity => {
        if (entity.name === 'item') {
          itemDropTrace.push({
            event: 'spawn',
            id: entity.id,
            position: entity.position && {
              x: entity.position.x,
              y: entity.position.y,
              z: entity.position.z
            },
            metadata: entity.metadata
          })
        }
      })
      bot.on('itemDrop', entity => {
        itemDropTrace.push({
          event: 'metadata',
          id: entity.id,
          position: entity.position && {
            x: entity.position.x,
            y: entity.position.y,
            z: entity.position.z
          },
          metadata: entity.metadata
        })
      })
      bot.on('entitySpawn', entity => {
        if (entity.name === 'arrow')
          rangedProjectileSpawns.push(entity.id)
      })
    })
    second = await connectBot('CppfmProbeB', gamePort)
    assert(first.entity && second.entity,
           'two 1.21.4 offline clients login and spawn')
    await waitUntil(() => firstInventorySyncObserved,
                    'fresh player inventory synchronization')
    assert(first.inventory.items().length === 0,
           'fresh vanilla-survival player starts with an empty inventory')
    assert(first.game.gameMode === 'survival' &&
           second.game.gameMode === 'survival',
      'fresh Mineflayer clients join in the vanilla survival default')

    const stoneId = first.registry.itemsByName.stone.id
    const stoneBricksId = first.registry.itemsByName.stone_bricks.id
    const slotDisplayHasItem = (slotDisplay, itemId) => {
      if (slotDisplay?.type === 'item')
        return slotDisplay.data === itemId
      if (slotDisplay?.type === 'item_stack')
        return slotDisplay.data?.itemId === itemId &&
          slotDisplay.data.itemCount > 0
      if (slotDisplay?.type === 'composite')
        return Array.isArray(slotDisplay.data) &&
          slotDisplay.data.some(child => slotDisplayHasItem(child, itemId))
      return false
    }
    const stonecuttingRecipe = recipeBookEntries.find(entry => {
      const display = entry.recipe?.display
      return display?.type === 'stonecutter' &&
        slotDisplayHasItem(display.data?.ingredient, stoneId) &&
        slotDisplayHasItem(display.data?.result, stoneBricksId) &&
        display.data?.craftingStation?.type === 'item' &&
        display.data.craftingStation.data ===
          first.registry.itemsByName.stonecutter.id
    })
    assert(stonecuttingRecipe,
      'server recipe book advertises the 1.21.4 stone-to-stone-bricks stonecutter recipe')
    const probeHorseArmorId =
      first.registry.itemsByName.diamond_horse_armor.id
    const probeIngredientId =
      first.registry.itemsByName.lapis_lazuli.id
    const hasProbeRecipe = entries => entries.some(entry => {
      const display = entry.recipe?.display
      return display?.type === 'crafting_shapeless' &&
        slotDisplayHasItem(display.data?.result, probeHorseArmorId) &&
        Array.isArray(display.data?.ingredients) &&
        display.data.ingredients.some(slot =>
          slotDisplayHasItem(slot, probeIngredientId))
    })
    assert(hasProbeRecipe(recipeBookEntries),
      'enabled world datapack recipe is advertised to a real client')
    const cutterFeet = first.entity.position.floored()
    const cutterPosition = new Vec3(cutterFeet.x + 2, cutterFeet.y,
                                   cutterFeet.z)
    await rcon.command(
      `setblock ${cutterPosition.x} ${cutterPosition.y} ` +
      `${cutterPosition.z} minecraft:stonecutter`)
    await rcon.command(`give ${first.username} minecraft:stone 7`)
    await waitUntil(() => countItems(first.inventory.items(), 'stone') === 7,
      'stonecutter recipe input to synchronize')
    const cutter = await waitUntil(() => {
      const block = first.blockAt(cutterPosition)
      return block?.name === 'stonecutter' ? block : null
    }, 'stonecutter fixture to reach the client')
    const openingSnapshot = nextWindowItems(
      first, packet => packet.windowId !== 0,
      'initial stonecutter menu snapshot')
    await first.activateBlock(cutter)
    const cutterWindow = await waitUntil(() => first.currentWindow,
      'Mineflayer stonecutter window to open')
    assert(cutterWindow,
      'Mineflayer opens the actual stonecutter menu')
    const initialMenuItems = await openingSnapshot
    assert(initialMenuItems.windowId === cutterWindow.id,
      'stonecutter opening snapshot belongs to the opened menu')
    const placedMenu = nextWindowItems(first, cutterWindow.id,
      'stonecutter makeAll recipe placement')
    first._client.write('craft_recipe_request', {
      windowId: cutterWindow.id,
      recipeId: stonecuttingRecipe.recipe.displayId,
      makeAll: true
    })
    await placedMenu
    await waitUntil(() => countItems(first.inventory.items(), 'stone') === 0,
      'authoritative player inventory update after stonecutter placement')
    const stonecutterPlacement = {
      recipeId: stonecuttingRecipe.recipe.displayId,
      windowId: cutterWindow.id,
      input: cutterWindow.slots[0] && {
        name: cutterWindow.slots[0].name,
        count: cutterWindow.slots[0].count
      },
      output: cutterWindow.slots[1] && {
        name: cutterWindow.slots[1].name,
        count: cutterWindow.slots[1].count
      },
      playerStone: countItems(first.inventory.items(), 'stone'),
      serverTail: child.output.split(/\r?\n/).slice(-12)
    }
    const stonecutterPlacementPassed =
      stonecutterPlacement.input?.name === 'stone' &&
      stonecutterPlacement.input?.count === 7 &&
      stonecutterPlacement.output?.name === 'stone_bricks' &&
      stonecutterPlacement.playerStone === 0
    if (!stonecutterPlacementPassed)
      throw new Error(`FAIL: stonecutter makeAll placement state: ` +
        JSON.stringify(stonecutterPlacement))
    assert(stonecutterPlacementPassed,
      'stonecutter makeAll places the full compatible input stack through a live recipe-book packet')
    await first.closeWindow(cutterWindow)
    await rcon.command(
      `setblock ${cutterPosition.x} ${cutterPosition.y} ` +
      `${cutterPosition.z} minecraft:air`)

    let rangedRevision = firstInventoryRevision
    await rcon.command(
      `item replace entity ${first.username} weapon.mainhand with minecraft:bow 1`)
    await waitUntil(() => firstInventoryRevision > rangedRevision &&
        countItems(first.inventory.items(), 'bow') === 1,
      'Infinity bow fixture to synchronize')
    rangedRevision = firstInventoryRevision
    const enchantResult = await rcon.command(
      `enchant ${first.username} minecraft:infinity 1`)
    assert(enchantResult.includes('Enchanted 1'),
      'operator can prepare a valid Infinity bow fixture')
    await waitUntil(() => firstInventoryRevision > rangedRevision,
      'Infinity enchantment component to synchronize')

    let rangedSpawnCount = rangedProjectileSpawns.length
    first.activateItem()
    await withAbort(sleep(300))
    first.deactivateItem()
    await withAbort(sleep(200))
    assert(rangedProjectileSpawns.length === rangedSpawnCount,
      'Infinity does not create an arrow when the survival player has no ammo')

    rangedRevision = firstInventoryRevision
    await rcon.command(`give ${first.username} minecraft:arrow 2`)
    await waitUntil(() => firstInventoryRevision > rangedRevision &&
        countItems(first.inventory.items(), 'arrow') === 2,
      'normal-arrow Infinity fixture to synchronize')
    rangedSpawnCount = rangedProjectileSpawns.length
    first.activateItem()
    await withAbort(sleep(1800))
    first.deactivateItem()
    await waitUntil(() => rangedProjectileSpawns.length > rangedSpawnCount,
      'charged Infinity bow to release a normal arrow', 8000)
    assert(countItems(first.inventory.items(), 'arrow') === 2,
      'Infinity preserves ordinary arrows after firing')

    rangedRevision = firstInventoryRevision
    await rcon.command(`clear ${first.username}`)
    await rcon.command(
      `item replace entity ${first.username} weapon.mainhand with minecraft:bow 1`)
    await rcon.command(`enchant ${first.username} minecraft:infinity 1`)
    await rcon.command(`give ${first.username} minecraft:spectral_arrow 1`)
    await waitUntil(() => firstInventoryRevision > rangedRevision &&
        countItems(first.inventory.items(), 'bow') === 1 &&
        countItems(first.inventory.items(), 'spectral_arrow') === 1,
      'Infinity special-arrow fixture to synchronize')
    rangedSpawnCount = rangedProjectileSpawns.length
    first.activateItem()
    await withAbort(sleep(1800))
    first.deactivateItem()
    await waitUntil(() => rangedProjectileSpawns.length > rangedSpawnCount,
      'charged Infinity bow to release a spectral arrow', 8000)
    assert(countItems(first.inventory.items(), 'spectral_arrow') === 0,
      'Infinity consumes spectral arrows instead of duplicating special ammo')

    rangedRevision = firstInventoryRevision
    await rcon.command(`clear ${first.username}`)
    await waitUntil(() => firstInventoryRevision > rangedRevision &&
        first.inventory.items().length === 0,
      'ranged-item fixtures to restore the empty inventory')

    let foodInventoryRevision = firstInventoryRevision
    await rcon.command(
      `item replace entity ${first.username} weapon.mainhand ` +
      'with minecraft:golden_apple 2')
    await waitUntil(() => firstInventoryRevision > foodInventoryRevision &&
        countItems(first.inventory.items(), 'golden_apple') === 2,
      'full-hunger golden-apple fixture to synchronize')
    foodInventoryRevision = firstInventoryRevision
    first.activateItem()
    await withAbort(sleep(250))
    assert(countItems(first.inventory.items(), 'golden_apple') === 2 &&
           !first.entity?.effects[10] && !first.entity?.effects[22],
      'golden apple remains unconsumed and unapplied before its use duration elapses')
    first.deactivateItem()
    await withAbort(sleep(350))
    assert(firstInventoryRevision === foodInventoryRevision &&
           countItems(first.inventory.items(), 'golden_apple') === 2 &&
           !first.entity?.effects[10] && !first.entity?.effects[22],
      'releasing a golden apple before its consume duration cancels use without consuming it')

    foodInventoryRevision = firstInventoryRevision
    first.activateItem()
    await waitUntil(() => firstInventoryRevision > foodInventoryRevision &&
        countItems(first.inventory.items(), 'golden_apple') === 1 &&
        first.entity?.effects[10]?.amplifier === 1 &&
        first.entity?.effects[22]?.amplifier === 0,
      'Mineflayer use-item packet to consume a golden apple at full hunger')
    first.deactivateItem()
    assert(countItems(first.inventory.items(), 'golden_apple') === 1,
      'FoodComponent always-edible golden apple works at full hunger')
    assert(first.entity.effects[10]?.amplifier === 1 &&
           first.entity.effects[22]?.amplifier === 0,
      'vanilla golden-apple ConsumableComponent applies Regeneration and Absorption')

    const saturationResult = await rcon.command(
      `effect give ${first.username} minecraft:saturation 10`)
    assert(saturationResult.includes('Applied'),
      'RCON applies the saturation fixture effect')
    await waitUntil(() => first.food === 20 && first.foodSaturation >= 19.9,
      'player to reach full food and saturation', 8000)
    foodInventoryRevision = firstInventoryRevision
    await rcon.command(
      `item replace entity ${first.username} weapon.mainhand ` +
      'with minecraft:golden_apple 1')
    await waitUntil(() => firstInventoryRevision > foodInventoryRevision &&
        countPlayerItems(first, 'golden_apple') === 1,
      'fully-saturated golden-apple fixture to synchronize')
    foodInventoryRevision = firstInventoryRevision
    first.activateItem()
    await waitUntil(() => firstInventoryRevision > foodInventoryRevision &&
        countPlayerItems(first, 'golden_apple') === 0,
      'always-edible food to consume even when food and saturation do not change')
    first.deactivateItem()

    foodInventoryRevision = firstInventoryRevision
    await rcon.command(
      `item replace entity ${first.username} weapon.mainhand ` +
      'with minecraft:cake 2')
    await waitUntil(() => firstInventoryRevision > foodInventoryRevision &&
        countItems(first.inventory.items(), 'cake') === 2,
      'cake-item fixture to synchronize')
    first.activateItem()
    await withAbort(sleep(250))
    first.deactivateItem()
    await withAbort(sleep(250))
    assert(countItems(first.inventory.items(), 'cake') === 2,
      'cake inventory item is not eaten by an air-use packet')

    foodInventoryRevision = firstInventoryRevision
    await rcon.command(
      `item replace entity ${first.username} weapon.offhand ` +
      'with minecraft:totem_of_undying 1')
    await waitUntil(() => firstInventoryRevision > foodInventoryRevision &&
        countPlayerItems(first, 'totem_of_undying') === 1,
      'offhand totem fixture to synchronize')
    await rcon.command(`damage ${first.username} 1000 minecraft:generic`)
    await waitUntil(() => first.health > 0 && first.health <= 1.01 &&
        countPlayerItems(first, 'totem_of_undying') === 0,
      'lethal damage to consume the offhand totem and preserve the player')
    assert(first.health > 0 && first.entity,
      'totem death protection keeps the Mineflayer session alive')
    await waitUntil(() => first.entity.effects[10]?.amplifier === 1 &&
        first.entity.effects[22]?.amplifier === 1 &&
        first.entity.effects[12]?.amplifier === 0,
      'totem Regeneration II, Absorption II, and Fire Resistance effects',
      5000)
    assert(first.entity.effects[10].duration === 900 &&
        first.entity.effects[22].duration === 100 &&
        first.entity.effects[12].duration === 800,
      'totem transmits the vanilla effect levels and durations')

    foodInventoryRevision = firstInventoryRevision
    await rcon.command(`clear ${first.username}`)
    await waitUntil(() => firstInventoryRevision > foodInventoryRevision &&
        first.inventory.items().length === 0,
      'food behavior fixtures to restore the initially empty inventory')

    const menuWireTrace = []
    const movementWireTrace = []
    const firstMovementWireTrace = []
    const interactionPositionTrace = []
    let captureObserverMovementWrites = null
    const stateIdsByWindow = new Map()
    const interactionBot = second
    interactionBot._client.on('position', packet => {
      interactionPositionTrace.push(`in position ${JSON.stringify(packet)}`)
      if (interactionPositionTrace.length > 24)
        interactionPositionTrace.shift()
    })
    const originalMovementWrite = interactionBot._client.write.bind(interactionBot._client)
    interactionBot._client.write = (name, packet, ...args) => {
      if (name === 'position' || name === 'position_look') {
        movementWireTrace.push(`out ${name} ${JSON.stringify(packet)}`)
        if (movementWireTrace.length > 16) movementWireTrace.shift()
      }
      return originalMovementWrite(name, packet, ...args)
    }
    interactionBot._client.on('position', packet => {
      movementWireTrace.push(`in position ${JSON.stringify(packet)}`)
      if (movementWireTrace.length > 16) movementWireTrace.shift()
    })
    const originalFirstMovementWrite = first._client.write.bind(first._client)
    first._client.write = (name, packet, ...args) => {
      if (captureObserverMovementWrites &&
          ['position', 'position_look', 'teleport_confirm'].includes(name)) {
        captureObserverMovementWrites.push({
          name,
          packet: JSON.parse(JSON.stringify(packet))
        })
        if (captureObserverMovementWrites.length > 64)
          captureObserverMovementWrites.shift()
      }
      if (name === 'position' || name === 'position_look') {
        firstMovementWireTrace.push(`out ${name} ${JSON.stringify(packet)}`)
        if (firstMovementWireTrace.length > 16)
          firstMovementWireTrace.shift()
      } else if (name === 'teleport_confirm') {
        firstMovementWireTrace.push(
          `out teleport_confirm ${JSON.stringify(packet)}`)
        if (firstMovementWireTrace.length > 16)
          firstMovementWireTrace.shift()
      }
      return originalFirstMovementWrite(name, packet, ...args)
    }
    first._client.on('position', packet => {
      firstMovementWireTrace.push(`in position ${JSON.stringify(packet)}`)
      if (firstMovementWireTrace.length > 16)
        firstMovementWireTrace.shift()
    })
    const rememberMenuPacket = (direction, name, packet) => {
      if (Number.isInteger(packet.windowId) &&
          Number.isInteger(packet.stateId))
        stateIdsByWindow.set(packet.windowId, packet.stateId)
      menuWireTrace.push(`${direction} ${name} window=${packet.windowId} ` +
        `state=${packet.stateId}`)
      if (menuWireTrace.length > 32) menuWireTrace.shift()
    }
    interactionBot._client.on('window_items', packet =>
      rememberMenuPacket('in', 'window_items', packet))
    interactionBot._client.on('set_slot', packet =>
      rememberMenuPacket('in', 'set_slot', packet))
    const originalClientWrite = interactionBot._client.write.bind(interactionBot._client)
    interactionBot._client.write = (name, packet, ...args) => {
      if (name === 'window_click')
        rememberMenuPacket('out', name, packet)
      if (name === 'teleport_confirm') {
        interactionPositionTrace.push(
          `out teleport_confirm ${JSON.stringify(packet)}`)
        if (interactionPositionTrace.length > 24)
          interactionPositionTrace.shift()
      }
      return originalClientWrite(name, packet, ...args)
    }
    const observedMover = await waitUntil(
      () => first.entities[interactionBot.entity.id],
      'first client to track the second player')

    const marker = `cppfm-mineflayer-${Date.now()}`
    const chatReceived = waitForChat(second, first.username, marker)
    first.chat(marker)
    await chatReceived

    // Carve a broad, isolated test platform around the actual spawn. Keep one
    // air block between the initial feet position and the floor so tiny
    // negative-Y rounding differences cannot make the collision probe treat
    // the floor as overlapping the player's feet.
    const spawnBlock = first.entity.position.floored()
    const floorY = spawnBlock.y - 2
    const movementPlatformXRadius = 12
    const movementPlatformMinZ = spawnBlock.z - 16
    const movementPlatformMaxZ = spawnBlock.z + 64
    const minX = spawnBlock.x - movementPlatformXRadius
    const maxX = spawnBlock.x + movementPlatformXRadius
    const minZ = movementPlatformMinZ
    const maxZ = movementPlatformMaxZ
    const floorResult = await rcon.command(
      `fill ${minX} ${floorY} ${minZ} ${maxX} ${floorY} ${maxZ} minecraft:stone`)
    const airResult = await rcon.command(
      `fill ${minX} ${floorY + 1} ${minZ} ${maxX} ${spawnBlock.y + 2} ${maxZ} minecraft:air`)
    console.log(`movement setup at ${spawnBlock}: ${floorResult}; ${airResult}`)
    await waitUntil(() => first.blockAt(new Vec3(spawnBlock.x, floorY, spawnBlock.z))?.name === 'stone' &&
                          first.blockAt(new Vec3(spawnBlock.x, floorY + 1, spawnBlock.z))?.name === 'air',
                    'movement platform updates')

    // Fresh clients initially share spawn coordinates. Give the interaction
    // client a nearby lane so mining and container fixtures have distinct
    // players while remaining inside the cleared platform.
    const moverStart = new Vec3(
      spawnBlock.x + 2.5, floorY + 1, spawnBlock.z - 2.5)
    const moverTeleportResult = await rcon.command(
      `tp ${interactionBot.username} ${moverStart.x} ` +
      `${moverStart.y} ${moverStart.z}`)
    await waitUntil(() => {
      const observed = first.entities[interactionBot.entity.id]
      return observed &&
        observed.position.distanceTo(moverStart) < 0.35 &&
        interactionBot.entity.position.distanceTo(moverStart) < 0.35
    }, 'movement client to reach its isolated platform lane')
    await waitUntil(() => first.entity.onGround &&
                          interactionBot.entity.onGround &&
                          Math.abs(first.entity.position.y - (floorY + 1)) < 0.1 &&
                          Math.abs(interactionBot.entity.position.y - (floorY + 1)) < 0.1 &&
                          Math.abs(observedMover.position.y - (floorY + 1)) < 0.1,
                    'both clients to observe stable footing on the movement platform')
    console.log(`movement client isolated at ${moverStart}: ${moverTeleportResult}`)

    assert(first.entity.onGround && interactionBot.entity.onGround,
      'both real clients are grounded before gameplay fixtures begin')
    console.log(`movement probe lane prepared at ${moverStart}: ${moverTeleportResult}`)

    const feet = first.entity.position.floored()
    const testY = feet.y - 1
    const stonePos = new Vec3(feet.x, testY, feet.z)
    const interactionFeet = interactionBot.entity.position.floored()
    const chestPos = new Vec3(
      interactionFeet.x + 1, interactionFeet.y - 1, interactionFeet.z)
    const lootSupportPos = new Vec3(stonePos.x, stonePos.y - 1, stonePos.z)
    await rcon.command(
      `setblock ${lootSupportPos.x} ${lootSupportPos.y} ` +
      `${lootSupportPos.z} minecraft:stone`)
    await rcon.command(`setblock ${stonePos.x} ${stonePos.y} ${stonePos.z} minecraft:dirt`)
    await rcon.command(`setblock ${chestPos.x} ${chestPos.y} ${chestPos.z} minecraft:chest`)
    await rcon.command(`give ${interactionBot.username} minecraft:diamond 3`)
    await waitUntil(() => countItems(interactionBot.inventory.items(), 'diamond') === 3,
                    'test items to arrive in player inventory')

    const diamondsBeforeLootCommand = countItems(first.inventory.items(), 'diamond')
    const ironBeforeLootCommand =
      countItems(first.inventory.items(), 'iron_ingot')
    let inventoryRevision = firstInventoryRevision
    await rcon.command(
      `loot give ${first.username} loot minecraft:chests/simple_dungeon`)
    await waitUntil(() => firstInventoryRevision > inventoryRevision,
      'registered chest loot command to synchronize its inventory result')
    await waitUntil(
      () => countItems(first.inventory.items(), 'diamond') ===
        diamondsBeforeLootCommand + 1,
      'loot give to evaluate the exact namespaced chest table')
    assert(countItems(first.inventory.items(), 'iron_ingot') ===
        ironBeforeLootCommand,
      '/loot give preserves the registered table result instead of inventing name-based fallback items')

    const diamondsBeforeEmptyLoot = countItems(first.inventory.items(), 'diamond')
    inventoryRevision = firstInventoryRevision
    await rcon.command(
      `loot give ${first.username} loot minecraft:blocks/empty`)
    await waitUntil(() => firstInventoryRevision > inventoryRevision,
      'valid empty loot result to synchronize its inventory')
    assert(countItems(first.inventory.items(), 'diamond') ===
        diamondsBeforeEmptyLoot,
      'a registered valid-empty loot table gives no fabricated fallback item')
    inventoryRevision = firstInventoryRevision
    await rcon.command(`clear ${first.username}`)
    await waitUntil(() => firstInventoryRevision > inventoryRevision &&
        countItems(first.inventory.items(), 'diamond') === 0,
      'command loot fixture cleanup to restore the empty miner inventory')

    const dirt = await waitUntil(() => {
      const block = first.blockAt(stonePos)
      return block && block.name === 'dirt' ? block : null
    }, 'test block to arrive at the client')
    const nuggetsBeforeDig =
      countItems(first.inventory.items(), 'gold_nugget') +
      countItems(interactionBot.inventory.items(), 'gold_nugget')
    await withTimeout(first.dig(dirt), 15000, 'block break')
    // Mineflayer predicts air locally as soon as its estimated dig timer
    // expires. That cache transition is not evidence that the server accepted
    // the dig; use the server's read-only block query as the authority before
    // asserting loot or pickup behavior.
    await waitForRconBlockState(
      rcon, stonePos, 'minecraft:air',
      'authoritative server block break after Mineflayer digging')
    try {
      await waitUntil(
        () => countItems(first.inventory.items(), 'gold_nugget') +
          countItems(interactionBot.inventory.items(), 'gold_nugget') >
            nuggetsBeforeDig,
        'custom 1.21.4 datapack loot to be picked up after mining', 15000)
    } catch (error) {
      let datapackState = 'unavailable'
      try { datapackState = await rcon.command('datapack list') } catch (_) {}
      console.error('custom-loot diagnostics: ' + JSON.stringify({
        position: stonePos,
        playerPosition: first.entity.position,
        lootSupport: await rcon.command(
          `data get block ${lootSupportPos.x} ${lootSupportPos.y} ${lootSupportPos.z}`),
        block: first.blockAt(stonePos)?.name,
        inventory: first.inventory.items().map(item => ({
          name: item.name,
          count: item.count
        })),
        nearbyPlayerInventory: interactionBot.inventory.items().map(item => ({
          name: item.name,
          count: item.count
        })),
        datapackState,
        itemDropTrace,
        nearbyItems: Object.values(first.entities)
          .filter(entity => entity.name === 'item')
          .map(entity => ({
            id: entity.id,
            position: entity.position && {
              x: entity.position.x,
              y: entity.position.y,
              z: entity.position.z
            },
            metadata: entity.metadata
          }))
      }))
      console.error(`server output at loot failure:\n${child.output.split(/\r?\n/).slice(-30).join('\n')}`)
      throw error
    }
    const nearbyPlayerNuggets =
      countItems(first.inventory.items(), 'gold_nugget') +
      countItems(interactionBot.inventory.items(), 'gold_nugget')
    assert(nearbyPlayerNuggets === nuggetsBeforeDig + 1,
      'mining uses namespaced 1.21.4 datapack loot and synchronizes one dropped item into a nearby player inventory')
    // Breaking the block directly below the miner removes its support. Wait
    // for ordinary client-side falling to settle onto the safety block before
    // later RCON teleports and menu-range checks.
    let previousMinerPosition = first.entity.position.clone()
    let stableMinerSamples = 0
    await waitUntil(() => {
      const current = first.entity.position.clone()
      const delta = current.distanceTo(previousMinerPosition)
      stableMinerSamples = first.entity.onGround && delta < 0.04
        ? stableMinerSamples + 1 : 0
      previousMinerPosition = current
      return stableMinerSamples >= 5 ? current : false
    }, 'miner to settle after breaking the supported loot block', 10000)
    const cloneSourceX = Math.floor(first.entity.position.x) + 5
    const cloneDestinationX = cloneSourceX + 4
    const cloneY = Math.floor(first.entity.position.y) - 1
    const cloneZ = Math.floor(first.entity.position.z)
    await rcon.command(
      `setblock ${cloneSourceX} ${cloneY} ${cloneZ} minecraft:oak_log[axis=x]`)
    await rcon.command(
      `setblock ${cloneSourceX + 1} ${cloneY} ${cloneZ} minecraft:oak_log[axis=y]`)
    await rcon.command(
      `setblock ${cloneDestinationX} ${cloneY} ${cloneZ} minecraft:dirt`)
    await rcon.command(
      `setblock ${cloneDestinationX + 1} ${cloneY} ${cloneZ} minecraft:dirt`)
    const cloneResult = await rcon.command(
      `clone ${cloneSourceX} ${cloneY} ${cloneZ} ` +
      `${cloneSourceX + 1} ${cloneY} ${cloneZ} ` +
      `${cloneDestinationX} ${cloneY} ${cloneZ} ` +
      'filtered minecraft:oak_log[axis=x]')
    assert(cloneResult.includes('Cloned 1 blocks'),
      '/clone filtered applies requested block-state properties')
    const filteredClone = await waitUntil(() => {
      const matched = first.blockAt(new Vec3(
        cloneDestinationX, cloneY, cloneZ))
      const rejected = first.blockAt(new Vec3(
        cloneDestinationX + 1, cloneY, cloneZ))
      return matched?.name === 'oak_log' && rejected?.name === 'dirt'
        ? { matched, rejected } : null
    }, 'property-filtered clone result to arrive at the client')
    assert(filteredClone.matched.getProperties().axis === 'x' &&
           filteredClone.rejected.name === 'dirt',
      '/clone filtered copies only matching axis states and preserves nonmatches')

    const maskedMoveX = cloneSourceX + 10
    await rcon.command(
      `setblock ${maskedMoveX} ${cloneY} ${cloneZ} minecraft:air`)
    await rcon.command(
      `setblock ${maskedMoveX + 1} ${cloneY} ${cloneZ} minecraft:stone`)
    await rcon.command(
      `setblock ${maskedMoveX + 2} ${cloneY} ${cloneZ} minecraft:dirt`)
    const maskedMoveResult = await rcon.command(
      `clone ${maskedMoveX} ${cloneY} ${cloneZ} ` +
      `${maskedMoveX + 1} ${cloneY} ${cloneZ} ` +
      `${maskedMoveX + 1} ${cloneY} ${cloneZ} masked move`)
    assert(maskedMoveResult.includes('Cloned 1 blocks'),
      '/clone masked move counts and shifts only non-air source blocks')
    const maskedMoveResultBlocks = await waitUntil(() => {
      const cleared = first.blockAt(new Vec3(
        maskedMoveX + 1, cloneY, cloneZ))
      const moved = first.blockAt(new Vec3(
        maskedMoveX + 2, cloneY, cloneZ))
      return cleared?.name === 'air' && moved?.name === 'stone'
        ? { cleared, moved } : null
    }, 'overlapping masked move to clear its selected source and write destination')
    assert(maskedMoveResultBlocks.cleared.name === 'air' &&
           maskedMoveResultBlocks.moved.name === 'stone',
      '/clone masked move does not duplicate a source skipped by the mask')

    const tagCloneSourceZ = cloneZ + 2
    const tagCloneDestinationX = cloneDestinationX + 4
    await rcon.command(
      `setblock ${cloneSourceX} ${cloneY} ${tagCloneSourceZ} ` +
      'minecraft:oak_log[axis=x]')
    await rcon.command(
      `setblock ${cloneSourceX + 1} ${cloneY} ${tagCloneSourceZ} ` +
      'minecraft:spruce_log[axis=x]')
    await rcon.command(
      `setblock ${cloneSourceX + 2} ${cloneY} ${tagCloneSourceZ} ` +
      'minecraft:oak_log[axis=y]')
    for (let offset = 0; offset < 3; offset++) {
      await rcon.command(
        `setblock ${tagCloneDestinationX + offset} ${cloneY} ${tagCloneSourceZ} ` +
        'minecraft:dirt')
    }
    const tagCloneResult = await rcon.command(
      `clone ${cloneSourceX} ${cloneY} ${tagCloneSourceZ} ` +
      `${cloneSourceX + 2} ${cloneY} ${tagCloneSourceZ} ` +
      `${tagCloneDestinationX} ${cloneY} ${tagCloneSourceZ} ` +
      'filtered #minecraft:logs[axis=x]')
    assert(tagCloneResult.includes('Cloned 2 blocks'),
      '/clone filtered supports vanilla block tags with partial state predicates')
    await waitUntil(() => {
      const oak = first.blockAt(new Vec3(
        tagCloneDestinationX, cloneY, tagCloneSourceZ))
      const spruce = first.blockAt(new Vec3(
        tagCloneDestinationX + 1, cloneY, tagCloneSourceZ))
      const rejected = first.blockAt(new Vec3(
        tagCloneDestinationX + 2, cloneY, tagCloneSourceZ))
      return oak?.name === 'oak_log' && spruce?.name === 'spruce_log' &&
        rejected?.name === 'dirt' ? { oak, spruce } : null
    }, 'tag-filtered clone result to arrive at the client')
    assert(first.blockAt(new Vec3(
      tagCloneDestinationX, cloneY, tagCloneSourceZ))
      .getProperties().axis === 'x' &&
      first.blockAt(new Vec3(
        tagCloneDestinationX + 1, cloneY, tagCloneSourceZ))
        .getProperties().axis === 'x',
      '/clone tag predicates apply partial block-state properties')

    const fillSourceZ = cloneZ + 4
    const fillMinX = cloneSourceX
    await rcon.command(
      `setblock ${fillMinX} ${cloneY} ${fillSourceZ} ` +
      'minecraft:oak_log[axis=x]')
    await rcon.command(
      `setblock ${fillMinX + 1} ${cloneY} ${fillSourceZ} ` +
      'minecraft:oak_log[axis=y]')
    await rcon.command(
      `setblock ${fillMinX + 2} ${cloneY} ${fillSourceZ} ` +
      'minecraft:spruce_log[axis=x]')
    const fillResult = await rcon.command(
      `fill ${fillMinX} ${cloneY} ${fillSourceZ} ` +
      `${fillMinX + 2} ${cloneY} ${fillSourceZ} minecraft:stone ` +
      'replace #minecraft:logs[axis=x]')
    assert(fillResult.includes('Filled 2 blocks'),
      '/fill replace supports tags and matches only explicitly requested state properties')
    await waitUntil(() => {
      const matchedOak = first.blockAt(new Vec3(fillMinX, cloneY, fillSourceZ))
      const rejectedOak = first.blockAt(new Vec3(fillMinX + 1, cloneY, fillSourceZ))
      const matchedSpruce = first.blockAt(new Vec3(fillMinX + 2, cloneY, fillSourceZ))
      return matchedOak?.name === 'stone' && rejectedOak?.name === 'oak_log' &&
        matchedSpruce?.name === 'stone' ? true : null
    }, 'block-tag /fill filter results to arrive at the client')

    const chest = await waitUntil(() => {
      const block = interactionBot.blockAt(chestPos)
      return block && block.name === 'chest' ? block : null
    }, 'test chest to arrive at the client')
    const observerChest = await waitUntil(() => {
      const block = first.blockAt(chestPos)
      return block && block.name === 'chest' ? block : null
    }, 'observer chest to arrive at the first client')
    first.clearControlStates()
    const observerSupport = new Vec3(
      chestPos.x + 1, chestPos.y, chestPos.z)
    const observerFeet = new Vec3(
      observerSupport.x + 0.5, observerSupport.y + 1,
      observerSupport.z + 0.5)
    await waitUntil(() =>
      first.blockAt(observerSupport)?.name === 'stone' &&
      first.blockAt(observerFeet)?.name === 'air' &&
      first.blockAt(observerFeet.offset(0, 1, 0))?.name === 'air',
    'observer support and two-block standing space beside the chest')
    const observerPositionPackets = []
    const observerMovementWrites = []
    const observerPhysicsPackets = []
    const observerPhysicsTicks = []
    captureObserverMovementWrites = observerMovementWrites
    const recordObserverPosition = packet => {
      observerPositionPackets.push({
        teleportId: packet.teleportId,
        x: packet.x,
        y: packet.y,
        z: packet.z,
        yaw: packet.yaw,
        pitch: packet.pitch,
        flags: packet.flags,
        playerPosition: first.entity?.position?.clone(),
        playerVelocity: first.entity?.velocity?.clone()
      })
      if (observerPositionPackets.length > 16)
        observerPositionPackets.shift()
    }
    const recordObserverPhysicsPacket = (name, packet) => {
      observerPhysicsPackets.push({ name, packet })
      if (observerPhysicsPackets.length > 32)
        observerPhysicsPackets.shift()
    }
    const recordObserverVelocity = packet =>
      recordObserverPhysicsPacket('entity_velocity', packet)
    const recordObserverExplosion = packet =>
      recordObserverPhysicsPacket('explosion', packet)
    const recordObserverPhysicsTick = () => {
      if (observerPhysicsTicks.length >= 32) return
      observerPhysicsTicks.push({
        position: first.entity?.position?.clone(),
        velocity: first.entity?.velocity?.clone(),
        onGround: first.entity?.onGround,
        collidedHorizontally: first.entity?.isCollidedHorizontally,
        collidedVertically: first.entity?.isCollidedVertically,
        controls: Object.fromEntries(
          ['forward', 'back', 'left', 'right', 'jump', 'sprint', 'sneak']
            .map(control => [control, first.getControlState(control)]))
      })
    }
    first._client.on('position', recordObserverPosition)
    first._client.on('entity_velocity', recordObserverVelocity)
    first._client.on('explosion', recordObserverExplosion)
    first.on('physicsTick', recordObserverPhysicsTick)
    const observerTeleportResult = await rcon.command(
      `tp ${first.username} ${observerFeet.x} ${observerFeet.y} ${observerFeet.z}`)
    try {
      await waitUntil(() => first.entity &&
        first.entity.position.distanceTo(observerFeet) < 0.25 &&
        !first.getControlState('forward'),
      'observer to stand beside the chest before opening the second GUI')
    } catch (error) {
      let authoritativePlayerPosition = 'unavailable'
      let onlinePlayers = 'unavailable'
      let executedPositionMarker = 'unavailable'
      let markerAtExpected = 'unavailable'
      let markerAtPrevious = 'unavailable'
      try {
        authoritativePlayerPosition = await rcon.command(
          `data get entity ${first.username} Pos`)
      } catch (_) {}
      try { onlinePlayers = await rcon.command('list') } catch (_) {}
      try {
        executedPositionMarker = await rcon.command(
          `execute as ${first.username} at @s run setblock ~ ~-1 ~ minecraft:gold_block`)
        markerAtExpected = await rcon.command(
          `data get block ${Math.floor(observerFeet.x)} ` +
          `${Math.floor(observerFeet.y - 1)} ${Math.floor(observerFeet.z)}`)
        markerAtPrevious = await rcon.command(
          `data get block ${Math.floor(playerFeatureOrigin.x + 0.5)} ` +
          `${Math.floor(playerFeatureOrigin.y - 1)} ` +
          `${Math.floor(playerFeatureOrigin.z + 0.5)}`)
      } catch (_) {}
      console.error('observer-teleport diagnostics: ' + JSON.stringify({
        commandResult: observerTeleportResult,
        authoritativePlayerPosition,
        onlinePlayers,
        executedPositionMarker,
        markerAtExpected,
        markerAtPrevious,
        expected: observerFeet,
        actual: first.entity?.position,
        receivedPositionPackets: observerPositionPackets,
        sentMovementPackets: observerMovementWrites,
        receivedPhysicsPackets: observerPhysicsPackets,
        physicsTicks: observerPhysicsTicks,
        velocity: first.entity?.velocity,
        controls: first.controlState,
        serverObserver: interactionBot.entities[first.entity?.id]?.position,
        interactionPlayer: interactionBot.entity.position,
        wire: firstMovementWireTrace
      }))
      console.error(`server output at observer-teleport failure:\n${child.output.slice(-6000)}`)
      throw error
    } finally {
      first._client.removeListener('position', recordObserverPosition)
      first._client.removeListener('entity_velocity', recordObserverVelocity)
      first._client.removeListener('explosion', recordObserverExplosion)
      first.removeListener('physicsTick', recordObserverPhysicsTick)
      captureObserverMovementWrites = null
    }
    try {
      observerContainer = await withTimeout(
        first.openContainer(observerChest), 15000, 'observer chest open')
    } catch (error) {
      const player = first.entity.position
      const blockPosition = observerChest.position
      const eyeY = player.y + 1.62
      const dx = Math.max(blockPosition.x - player.x, 0,
                          player.x - blockPosition.x - 1)
      const dy = Math.max(blockPosition.y - eyeY, 0,
                          eyeY - blockPosition.y - 1)
      const dz = Math.max(blockPosition.z - player.z, 0,
                          player.z - blockPosition.z - 1)
      const authoritativeBlock = await rcon.command(
        `data get block ${blockPosition.x} ${blockPosition.y} ${blockPosition.z}`)
      const authoritativePlayerPosition = await rcon.command(
        `data get entity ${first.username} Pos`)
      console.error('observer-open diagnostics: ' + JSON.stringify({
        observer: player,
        observerVelocity: first.entity.velocity,
        controls: first.controlState,
        authoritativePlayerPosition,
        receivedPositionPackets: observerPositionPackets,
        sentMovementPackets: observerMovementWrites,
        receivedPhysicsPackets: observerPhysicsPackets,
        physicsTicks: observerPhysicsTicks,
        firstClientWire: firstMovementWireTrace,
        movementWireTrace,
        interactingPlayer: interactionBot.entity.position,
        block: blockPosition,
        eyeToBlockDistance: Math.sqrt(dx * dx + dy * dy + dz * dz),
        currentWindow: first.currentWindow?.id ?? null,
        authoritativeBlock
      }))
      console.error(`server output at observer-open failure:\n${child.output}`)
      throw error
    }
    container = await withTimeout(interactionBot.openContainer(chest), 15000,
                                  'chest open')
    const diamondId = interactionBot.registry.itemsByName.diamond.id
    await withTimeout(container.deposit(diamondId, null, 1), 10000,
                       'chest deposit')
    await waitUntil(() => container.containerItems().some(
      item => item.name === 'diamond' && item.count === 1),
    'deposited item to arrive in chest inventory')
    await withTimeout(container.withdraw(diamondId, null, 1), 10000,
                       'chest withdrawal')
    await waitUntil(() => countItems(interactionBot.inventory.items(), 'diamond') === 3,
                    'withdrawn item to arrive in player inventory')
    await waitUntil(() => !container.containerItems().some(
      item => item.name === 'diamond'),
    'withdrawn item to leave the chest before the next transaction')

    const menuCloneSource = new Vec3(chestPos.x + 3, chestPos.y, chestPos.z)
    await rcon.command(
      `setblock ${menuCloneSource.x} ${menuCloneSource.y} ` +
      `${menuCloneSource.z} minecraft:chest`)
    await rcon.command(
      `item replace block ${menuCloneSource.x} ${menuCloneSource.y} ` +
      `${menuCloneSource.z} container.0 with minecraft:emerald 2`)
    const openMenuClone = await rcon.command(
      `clone ${menuCloneSource.x} ${menuCloneSource.y} ${menuCloneSource.z} ` +
      `${menuCloneSource.x} ${menuCloneSource.y} ${menuCloneSource.z} ` +
      `${chestPos.x} ${chestPos.y} ${chestPos.z} replace`)
    assert(openMenuClone.includes('Cloned 1 blocks'),
      'server clone replaces a chest while another client keeps its GUI open')
    await waitUntil(() => container.containerItems().some(
      item => item.name === 'emerald' && item.count === 2),
    'clone result to refresh the already-open chest GUI')
    await waitUntil(() => observerContainer.containerItems().some(
      item => item.name === 'emerald' && item.count === 2),
    'clone result to refresh the second already-open chest GUI')
    assert(container.containerItems().some(
      item => item.name === 'emerald' && item.count === 2) &&
      observerContainer.containerItems().some(
        item => item.name === 'emerald' && item.count === 2),
    '/clone immediately resynchronizes every open container window')

    container.close()
    await waitUntil(() => !interactionBot.currentWindow,
                    'the first chest window to close before the next fixture')
    container = null
    observerContainer.close()
    await waitUntil(() => !first.currentWindow,
                    'observer chest window to close before the next fixture')
    observerContainer = null
    const seededInventory = await rcon.command(
      `item replace block ${chestPos.x} ${chestPos.y} ${chestPos.z} ` +
      'container.0 with minecraft:diamond 1')
    assert(seededInventory.includes('Replaced block'),
      'server command seeds a persisted chest inventory for overwrite testing')
    const featureResult = await rcon.command(
      `place feature minecraft:oak ${chestPos.x} ${chestPos.y} ${chestPos.z}`)
    assert(featureResult.includes('no vanilla configured-feature registry is loaded'),
      '/place feature fails closed and leaves an existing block entity alone')
    const preservedChest = await waitUntil(() => {
      const block = interactionBot.blockAt(chestPos)
      return block && block.name === 'chest' ? block : null
    }, 'block entity to remain after the fail-closed feature request')
    container = await withTimeout(interactionBot.openContainer(preservedChest),
                                  15000, 'preserved chest open')
    try {
      await waitUntil(() => container.containerItems().some(
        item => item.name === 'diamond' && item.count === 1),
      'block-entity inventory to remain after the fail-closed feature request')
    } catch (error) {
      console.error('feature-fail-closed diagnostics: ' + JSON.stringify({
        featureResult,
        block: interactionBot.blockAt(chestPos)?.name,
        window: container.containerItems().map(
          item => [item.name, item.count, item.slot]),
        playerInventory: interactionBot.inventory.items().map(
          item => [item.name, item.count, item.slot])
      }))
      console.error(`server output after feature preservation failure:\n${child.output}`)
      throw error
    }
    container.close()
    await waitUntil(() => !interactionBot.currentWindow,
                    'preserved chest window to close before cleanup')
    container = null
    await rcon.command(
      `setblock ${chestPos.x} ${chestPos.y} ${chestPos.z} minecraft:air`)
    await waitUntil(() => interactionBot.blockAt(chestPos)?.name === 'air',
                    'the empty first chest to clear the destroy-test area')

    const treeOrigin = new Vec3(chestPos.x + 8, chestPos.y + 1, chestPos.z)
    await rcon.command(
      `setblock ${treeOrigin.x} ${treeOrigin.y} ${treeOrigin.z} minecraft:stone`)
    await waitUntil(() => interactionBot.blockAt(treeOrigin)?.name === 'stone',
                    'known-feature sentinel block to arrive at the client')
    const formerlyAdvertisedTree = await rcon.command(
      `place feature minecraft:oak ${treeOrigin.x} ${treeOrigin.y} ${treeOrigin.z}`)
    assert(formerlyAdvertisedTree.includes(
      'no vanilla configured-feature registry is loaded'),
    'former fixed-template oak mapping now fails closed')
    await waitUntil(() => interactionBot.blockAt(treeOrigin)?.name === 'stone',
                    'failed oak placement to preserve the sentinel block')

    const orePos = new Vec3(treeOrigin.x + 8, treeOrigin.y, treeOrigin.z)
    await rcon.command(
      `setblock ${orePos.x} ${orePos.y} ${orePos.z} minecraft:diamond_block`)
    await waitUntil(() => interactionBot.blockAt(orePos)?.name === 'diamond_block',
                    'unsupported-feature sentinel to arrive at the client')
    const unsupportedFeature = await rcon.command(
      `place feature example:unknown ${orePos.x} ${orePos.y} ${orePos.z}`)
    assert(unsupportedFeature.includes(
      'no vanilla configured-feature registry is loaded'),
    'unknown configured-feature ID returns a failure')
    await waitUntil(() => interactionBot.blockAt(orePos)?.name === 'diamond_block',
                    'failed unknown feature placement to preserve the sentinel')

    const staleClientPosition = first.entity.position.clone()
    const playerCommandOrigin = new Vec3(
      Math.floor(staleClientPosition.x) + 2,
      Math.floor(staleClientPosition.y),
      Math.floor(staleClientPosition.z))
    await rcon.command(
      `fill ${playerCommandOrigin.x - 2} ${playerCommandOrigin.y} ` +
      `${playerCommandOrigin.z - 2} ${playerCommandOrigin.x + 2} ` +
      `${playerCommandOrigin.y + 6} ${playerCommandOrigin.z + 2} minecraft:air`)
    await rcon.command(
      `fill ${playerCommandOrigin.x} ${playerCommandOrigin.y - 1} ` +
      `${playerCommandOrigin.z - 1} ${playerCommandOrigin.x} ` +
      `${playerCommandOrigin.y - 1} ${playerCommandOrigin.z + 2} minecraft:dirt`)
    await waitUntil(() =>
      first.blockAt(new Vec3(
        playerCommandOrigin.x, playerCommandOrigin.y - 1,
        playerCommandOrigin.z))?.name === 'dirt' &&
      first.blockAt(playerCommandOrigin)?.name === 'air' &&
      first.blockAt(playerCommandOrigin.offset(0, 1, 0))?.name === 'air',
    'teleport fixture support and standing space to arrive at the client')
    const operatorResult = await rcon.command(`op ${first.username}`)
    assert(operatorResult.includes(first.username),
      'RCON grants operator permission to the in-game command client')
    const commandFeedback = []
    const rememberCommandFeedback = message => {
      commandFeedback.push(String(message))
      if (commandFeedback.length > 16) commandFeedback.shift()
    }
    first.on('messagestr', rememberCommandFeedback)
    // Hold the client's teleport acknowledgement long enough to put an old
    // position packet ahead of it on the same TCP stream. A vanilla client can
    // have such a frame queued when the server initiates a teleport. The
    // player command below runs before the held acknowledgement, making its
    // relative block edit a direct observation of the server-side player pose.
    const clientWrite = first._client.write
    let heldTeleportConfirm = null
    first._client.write = function (name, params, ...args) {
      if (name === 'teleport_confirm' && heldTeleportConfirm === null) {
        heldTeleportConfirm = params
        return
      }
      return clientWrite.call(this, name, params, ...args)
    }
    let teleportResult
    try {
      teleportResult = await rcon.command(
        `tp ${first.username} ${playerCommandOrigin.x + 0.5} ` +
        `${playerCommandOrigin.y} ${playerCommandOrigin.z + 0.5}`)
      await waitUntil(() => heldTeleportConfirm,
        'Mineflayer to receive the command teleport before confirming it')
      clientWrite.call(first._client, 'position', {
        x: staleClientPosition.x,
        y: staleClientPosition.y,
        z: staleClientPosition.z,
        flags: { onGround: true, hasHorizontalCollision: false }
      })
      first.chat('/execute if block ~ ~-1 ~ minecraft:dirt run setblock ~ ~-1 ~ minecraft:gold_block')
      const expectedBlockFeedback = `Changed the block at ${playerCommandOrigin.x}, ` +
        `${playerCommandOrigin.y - 1}, ${playerCommandOrigin.z}`
      await waitUntil(
        () => commandFeedback.find(message =>
          message.includes(expectedBlockFeedback)),
        'relative block command to resolve at the teleported player position')
      const playerAtResponse = first.entity.position.floored()
      assert(playerAtResponse.x === playerCommandOrigin.x &&
        playerAtResponse.y === playerCommandOrigin.y &&
        playerAtResponse.z === playerCommandOrigin.z,
      `old movement is ignored until teleport acknowledgement and relative ` +
        `commands use the destination (player=${playerAtResponse}, ` +
        `expected=${playerCommandOrigin})`)
      clientWrite.call(first._client, 'teleport_confirm', heldTeleportConfirm)
      heldTeleportConfirm = null
      await waitUntil(
        () => first.blockAt(new Vec3(
          playerCommandOrigin.x, playerCommandOrigin.y - 1,
          playerCommandOrigin.z))?.name === 'gold_block',
        'relative setblock effect to arrive at the teleported player position')
      await rcon.command(
        `setblock ${playerCommandOrigin.x} ${playerCommandOrigin.y - 1} ` +
        `${playerCommandOrigin.z} minecraft:dirt`)
    } catch (error) {
      console.error('teleport/relative command diagnostics: ' + JSON.stringify({
        teleportResult,
        staleClientPosition,
        origin: playerCommandOrigin,
        heldTeleportConfirm,
        currentPlayerBlock: first.entity?.position?.floored(),
        player: first.entity?.position,
        velocity: first.entity?.velocity,
        onGround: first.entity?.onGround,
        firstClientWire: firstMovementWireTrace.slice(-12),
        soil: first.blockAt(new Vec3(
          playerCommandOrigin.x, playerCommandOrigin.y - 1,
          playerCommandOrigin.z))?.name,
        originBlock: first.blockAt(playerCommandOrigin)?.name,
        upperBlock: first.blockAt(new Vec3(
          playerCommandOrigin.x, playerCommandOrigin.y + 6,
          playerCommandOrigin.z))?.name,
        feedback: commandFeedback
      }))
      console.error(`server output during teleport/relative command test:\n${child.output}`)
      throw error
    } finally {
      first._client.write = clientWrite
      if (heldTeleportConfirm !== null)
        clientWrite.call(first._client, 'teleport_confirm', heldTeleportConfirm)
      first.removeListener('messagestr', rememberCommandFeedback)
    }
    const signPlayerBlock = first.entity.position.floored()
    const signPos = new Vec3(signPlayerBlock.x + 3,
      signPlayerBlock.y, signPlayerBlock.z + 3)
    await rcon.command(
      `setblock ${signPos.x} ${signPos.y} ${signPos.z} minecraft:oak_sign`)
    const signBlock = await waitUntil(() => {
      const block = first.blockAt(signPos)
      return block && block.name === 'oak_sign' ? block : null
    }, 'live sign block to arrive at the client')
    const signLines = [
      'cppfm-live-sign', 'second line', 'third line', 'fourth line'
    ]
    let signTileEntityPacket = null
    const signFeedback = []
    const rememberSignFeedback = message => signFeedback.push(String(message))
    const rememberSignPacket = packet => {
      if (packet.location?.x === signPos.x &&
          packet.location?.y === signPos.y &&
          packet.location?.z === signPos.z)
        signTileEntityPacket = packet
    }
    first.on('messagestr', rememberSignFeedback)
    first._client.on('tile_entity_data', rememberSignPacket)
    try {
      first.updateSign(signBlock, signLines.join('\n'))
      const updatedSignLines = await waitUntil(() => {
        // Require an actual server-authored tile-entity response. Mineflayer
        // updates its cached block entity optimistically when updateSign is
        // called, so polling blockAt() alone would not prove admission.
        const lines = signTextLinesFromNbt(signTileEntityPacket?.nbtData)
        return lines && lines[0] === signLines[0] ? lines : null
      }, 'server to admit and echo the edited sign block entity')
      assert(updatedSignLines.every((line, index) => line === signLines[index]),
        'live client sign editing receives the server-authored block-entity text')
    } catch (error) {
      console.error('sign-edit diagnostics: ' + JSON.stringify({
        position: signPos,
        player: first.entity?.position,
        block: first.blockAt(signPos)?.name,
        entity: first.blockAt(signPos)?.entity,
        tileEntityPacket: signTileEntityPacket,
        feedback: signFeedback
      }))
      console.error(`server output during sign edit:\n${child.output}`)
      throw error
    } finally {
      first.removeListener('messagestr', rememberSignFeedback)
      first._client.removeListener('tile_entity_data', rememberSignPacket)
    }

    const clonedSignPos = new Vec3(signPos.x + 2,
      signPos.y, signPos.z)
    await rcon.command(
      `setblock ${clonedSignPos.x} ${clonedSignPos.y} ` +
      `${clonedSignPos.z} minecraft:oak_sign`)
    await waitUntil(() => first.blockAt(clonedSignPos)?.name === 'oak_sign' &&
                         second.blockAt(clonedSignPos)?.name === 'oak_sign',
                    'clone destination sign to be tracked by both live clients')
    let clonedSignPacket = null
    const rememberClonedSign = packet => {
      if (packet.location?.x === clonedSignPos.x &&
          packet.location?.y === clonedSignPos.y &&
          packet.location?.z === clonedSignPos.z)
        clonedSignPacket = packet
    }
    second._client.on('tile_entity_data', rememberClonedSign)
    try {
      const clonedSignResult = await rcon.command(
        `clone ${signPos.x} ${signPos.y} ${signPos.z} ` +
        `${signPos.x} ${signPos.y} ${signPos.z} ` +
        `${clonedSignPos.x} ${clonedSignPos.y} ${clonedSignPos.z} replace`)
      assert(clonedSignResult.includes('Cloned 1 blocks'),
        '/clone copies a sign block entity to a loaded destination')
      const clonedLines = await waitUntil(() => {
        const lines = signTextLinesFromNbt(clonedSignPacket?.nbtData)
        return lines && lines[0] === signLines[0] ? lines : null
      }, 'clone block-entity update to reach the already-tracking second client')
      assert(clonedLines.every((line, index) => line === signLines[index]),
        '/clone immediately synchronizes sign text to existing observers')
    } finally {
      second._client.removeListener('tile_entity_data', rememberClonedSign)
    }

    // Exercise the real client block-dig packet path in all relevant modes.
    // Adventure without a can_break component and Spectator must not mutate a
    // block; Survival must retain delayed mining and Creative remains instant.
    const modeFeet = first.entity.position.floored()
    const modeTestPos = new Vec3(modeFeet.x + 2, modeFeet.y - 1, modeFeet.z)
    const tryMineFromClient = async (description, mode) => {
      const block = await waitUntil(() => {
        const current = first.blockAt(modeTestPos)
        return current && current.name === 'dirt' ? current : null
      }, `${description} dirt block to reach the client`)
      let digPackets = 0
      const digActions = []
      const originalWrite = first._client.write
      first._client.write = function (name, packet, ...args) {
        if (name === 'block_dig') {
          digPackets++
          digActions.push({
            status: packet.status,
            location: packet.location,
            face: packet.face
          })
        }
        return originalWrite.call(this, name, packet, ...args)
      }
      let digError = null
      try {
        if (mode === 'adventure' || mode === 'spectator') {
          // These modes are expected to reject the action. Send the actual
          // Mineflayer protocol packet directly so the test does not mistake
          // a client-side promise rejection for a server-side policy check.
          first._client.write('block_dig', {
            status: 0, location: block.position, face: 1, sequence: 0
          })
          await sleep(25)
          first._client.write('block_dig', {
            status: 2, location: block.position, face: 1, sequence: 0
          })
        } else {
          // Survival and Creative must complete the real Mineflayer API call;
          // merely seeing a START packet is not enough to pass this test.
          await withTimeout(first.dig(block), 12000, description)
        }
      } catch (error) {
        digError = error
      } finally {
        first._client.write = originalWrite
        if (digError && first.targetDigBlock) first.stopDigging()
      }
      if (digError) throw digError
      const actionsForTarget = digActions.filter(action =>
        action.location?.x === modeTestPos.x &&
        action.location?.y === modeTestPos.y &&
        action.location?.z === modeTestPos.z)
      assert(actionsForTarget.some(action => action.status === 0),
        `${description} sends a targeted START player-action packet`)
      if (mode === 'survival') {
        const sentStop = actionsForTarget.some(action => action.status === 2)
        const earlyAuthoritativeCompletion = sentStop ? '' :
          await waitForRconBlockState(
            rcon, modeTestPos, 'minecraft:air',
            `${description} to finish on the server before Mineflayer's scheduled STOP`,
            15000)
        assert(sentStop ||
               earlyAuthoritativeCompletion.includes('minecraft:air state='),
          `${description} completes with a targeted STOP or a server-confirmed block update before Mineflayer's scheduled STOP`)
      }
      return digActions
    }
    const setModeTestDirt = async () => {
      const miningPosition = new Vec3(
        modeTestPos.x - 2.5, modeTestPos.y + 1, modeTestPos.z + 0.5)
      const teleportResult = await rcon.command(
        `tp ${first.username} ${miningPosition.x} ${miningPosition.y} ` +
        `${miningPosition.z}`)
      try {
        await waitUntil(() =>
          first.entity.position.distanceTo(miningPosition) < 0.5,
        'player to stand within reach of the gamemode test block')
      } catch (error) {
        console.error('gamemode test teleport diagnostics: ' + JSON.stringify({
          teleportResult,
          target: miningPosition,
          actual: first.entity.position,
          velocity: first.entity.velocity,
          controls: first.controlState,
          observer: interactionBot.entities[first.entity.id]?.position,
          nearbyBlock: first.blockAt(miningPosition.floored())?.name,
          movementWire: firstMovementWireTrace
        }))
        throw error
      }
      await rcon.command(
        `setblock ${modeTestPos.x} ${modeTestPos.y} ${modeTestPos.z} minecraft:dirt`)
      await waitUntil(() => first.blockAt(modeTestPos)?.name === 'dirt',
                      'gamemode block fixture to reach the client')
    }
    const setGamemodeFromRcon = async mode => {
      const modeEvents = []
      const relatedPackets = []
      const observeModeEvent = packet => modeEvents.push(packet)
      const observeRelatedPacket = (packet, metadata) => {
        if (metadata?.name === 'game_state_change' ||
            metadata?.name === 'abilities') {
          relatedPackets.push({ name: metadata.name, packet })
        }
      }
      first._client.on('game_state_change', observeModeEvent)
      first._client.on('packet', observeRelatedPacket)
      try {
        const response = await rcon.command(
          `gamemode ${mode} ${first.username}`)
        assert(response.includes('Updated gamemode for 1'),
          `RCON gamemode ${mode} targets exactly one player (response: ${response})`)
        const modeId = {
          survival: 0,
          creative: 1,
          adventure: 2,
          spectator: 3
        }[mode]
        await waitUntil(() => modeEvents.some(packet =>
          packet.reason === 'change_game_mode' &&
          packet.gameMode === modeId),
        `Mineflayer to receive the ${mode} Game Event`)
        const abilities = await waitUntil(() => relatedPackets.find(entry =>
          entry.name === 'abilities' && entry.packet &&
          Number.isFinite(entry.packet.flags)),
        `Mineflayer to receive ${mode} Abilities`)
        const flags = abilities.packet.flags & 0xff
        if (mode === 'creative') {
          assert((flags & 0x0d) === 0x0d,
            'Creative Abilities enable invulnerability, flight, and instant-build')
        } else if (mode === 'spectator') {
          assert((flags & 0x06) === 0x06 && (flags & 0x09) === 0,
            'Spectator Abilities enable flight without Creative flags')
        } else {
          assert(flags === 0,
            `${mode} Abilities disable flight and Creative flags`)
        }
        assert(Math.abs(abilities.packet.flyingSpeed - 0.05) < 1e-6 &&
               Math.abs(abilities.packet.walkingSpeed -
                 (mode === 'creative' ? 0.10 : 0.05)) < 1e-6,
          `${mode} Abilities carry the expected movement speeds`)
        // minecraft-data 3.117.0 decodes the enum reason as the string
        // "change_game_mode", while Mineflayer 4.25.0 compares it to numeric
        // 3 and therefore leaves bot.game.gameMode stale. Keep the test bot's
        // convenience mirror aligned with the already-asserted wire event.
        first.game.gameMode = mode
      } catch (error) {
        console.error(`gamemode ${mode} diagnostics: ` + JSON.stringify({
          observedGameMode: first.game.gameMode,
          gameStateEvents: modeEvents,
          relatedPackets
        }))
        throw error
      } finally {
        first._client.removeListener('game_state_change', observeModeEvent)
        first._client.removeListener('packet', observeRelatedPacket)
      }
    }
    await setModeTestDirt()
    await setGamemodeFromRcon('adventure')
    await tryMineFromClient('Adventure without can_break', 'adventure')
    await waitForRconBlockState(rcon, modeTestPos, 'minecraft:dirt',
      'Adventure without can_break to leave the block intact')
    await setModeTestDirt()
    await setGamemodeFromRcon('spectator')
    await tryMineFromClient('Spectator block-break attempt', 'spectator')
    await waitForRconBlockState(rcon, modeTestPos, 'minecraft:dirt',
      'Spectator block-break attempt to leave the block intact')
    await setGamemodeFromRcon('survival')
    const survivalDigActions = await tryMineFromClient(
      'Survival block break', 'survival')
    try {
      await waitForRconBlockState(rcon, modeTestPos, 'minecraft:air',
        'Survival dig to remove the test block')
    } catch (error) {
      console.error('survival dig diagnostics: ' + JSON.stringify({
        target: modeTestPos,
        playerPosition: first.entity.position,
        gameMode: first.game.gameMode,
        digActions: survivalDigActions
      }))
      console.error(`server output during survival dig:\n${child.output.slice(-6000)}`)
      throw error
    }

    // A legitimate client can reach its local completion point just before
    // the server's next mining tick. Send an intentionally early STOP after a
    // real START and verify the server keeps its authoritative progress rather
    // than cancelling a completed-in-client dig.
    await setModeTestDirt()
    const earlyDigActions = []
    const originalDigWrite = first._client.write
    first._client.write = function (name, packet, ...args) {
      if (name === 'block_dig')
        earlyDigActions.push({ status: packet.status, location: packet.location })
      return originalDigWrite.call(this, name, packet, ...args)
    }
    try {
      first._client.write('block_dig', {
        status: 0, location: modeTestPos, face: 1, sequence: 0
      })
      await sleep(10)
      first._client.write('block_dig', {
        status: 2, location: modeTestPos, face: 1, sequence: 0
      })
    } finally {
      first._client.write = originalDigWrite
    }
    assert(earlyDigActions.map(action => action.status).join(',') === '0,2',
      'early-finish regression sends START and STOP player-action packets')
    await waitForRconBlockState(rcon, modeTestPos, 'minecraft:air',
      'early client completion to finish at the server mining deadline')

    await setModeTestDirt()
    await setGamemodeFromRcon('creative')
    await tryMineFromClient('Creative instant block break', 'creative')
    await waitForRconBlockState(rcon, modeTestPos, 'minecraft:air',
      'Creative dig to remove the test block immediately')
    await setGamemodeFromRcon('survival')

    // This uses Mineflayer's ordinary placeBlock API (not an RCON-prepared
    // block), and checks both the server state and survival item consumption.
    const placeFeet = first.entity.position.floored()
    const placeSupportPos = new Vec3(
      placeFeet.x + 2, placeFeet.y - 1, placeFeet.z)
    const placePos = new Vec3(
      placeSupportPos.x, placeSupportPos.y + 1, placeSupportPos.z)
    await rcon.command(
      `setblock ${placeSupportPos.x} ${placeSupportPos.y} ` +
      `${placeSupportPos.z} minecraft:stone`)
    await rcon.command(
      `setblock ${placePos.x} ${placePos.y} ${placePos.z} minecraft:air`)
    await rcon.command(`give ${first.username} minecraft:cobblestone 1`)
    await waitUntil(() => countItems(first.inventory.items(), 'cobblestone') === 1,
                    'placement item to arrive in the Survival inventory')
    const placementItem = first.inventory.items().find(
      item => item.name === 'cobblestone')
    await first.equip(placementItem, 'hand')
    assert(first.heldItem?.name === 'cobblestone',
      'Mineflayer equips the Survival placement item in its active hand')
    const placeSupport = await waitUntil(() => {
      const block = first.blockAt(placeSupportPos)
      return block && block.name === 'stone' ? block : null
    }, 'player-placement support block to reach the client')
    await withTimeout(first.placeBlock(placeSupport, new Vec3(0, 1, 0)),
                      15000, 'ordinary survival block placement')
    const placedServerState = await waitForRconBlockState(
      rcon, placePos, 'minecraft:cobblestone',
      'ordinary Mineflayer placement to commit on the server')
    await waitUntil(() => countItems(first.inventory.items(), 'cobblestone') === 0,
                    'successful survival placement to consume its item')
    assert(placedServerState.includes('minecraft:cobblestone state='),
      'ordinary Mineflayer block placement updates the world and consumes the held item')

    const packName = 'mineflayer_probe'
    await rcon.command(`datapack disable ${packName}`)
    await rcon.command(`datapack enable ${packName} first`)
    let packOrder = enabledDatapackOrder(await rcon.command('datapack list'))
    assert(packOrder[0] === packName,
      '/datapack enable first places a pack at the beginning of priority order')
    await rcon.command(`datapack disable ${packName}`)
    await rcon.command(`datapack enable ${packName} after vanilla`)
    packOrder = enabledDatapackOrder(await rcon.command('datapack list'))
    assert(packOrder.indexOf(packName) === packOrder.indexOf('vanilla') + 1,
      '/datapack enable after inserts immediately after the referenced pack')
    await rcon.command(`datapack disable ${packName}`)
    await rcon.command(`datapack enable ${packName} before vanilla`)
    packOrder = enabledDatapackOrder(await rcon.command('datapack list'))
    assert(packOrder.indexOf(packName) + 1 === packOrder.indexOf('vanilla'),
      '/datapack enable before inserts immediately before the referenced pack')
    await rcon.command(`datapack disable ${packName}`)
    await rcon.command(`datapack enable ${packName} last`)
    packOrder = enabledDatapackOrder(await rcon.command('datapack list'))
    assert(packOrder.at(-1) === packName,
      '/datapack enable last restores the highest-priority position')

    const checkProbeRecipeForNewClient = async (username, expected, label) => {
      const entries = []
      const bot = await connectBot(username, gamePort, client => {
        client._client.on('recipe_book_add', packet => {
          entries.push(...(packet.entries || []))
        })
      })
      try {
        await waitUntil(() => entries.length > 0,
          `${username} recipe book synchronization`)
        assert(hasProbeRecipe(entries) === expected, label)
      } finally {
        await stopBot(bot)
      }
    }

    const checkProbeRecipeForConnectedClient = async (
      action, expected, label
    ) => {
      const previousRevision = recipeBookRevision
      await action()
      await waitUntil(() => recipeBookRevision > previousRevision,
        `${label}: connected client recipe-book refresh`)
      assert(hasProbeRecipe(recipeBookEntries) === expected, label)
    }

    await checkProbeRecipeForConnectedClient(
      () => rcon.command(`datapack disable ${packName}`), false,
      'disabling a datapack removes its recipe from an already connected client')
    await checkProbeRecipeForNewClient(
      'RecipeOffA', false,
      'disabling a datapack removes its recipe from a newly joined client')
    await checkProbeRecipeForConnectedClient(
      () => rcon.command(`datapack enable ${packName} last`), true,
      'enabling a datapack publishes its recipe to an already connected client')
    await checkProbeRecipeForNewClient(
      'RecipeOnA', true,
      'enabling a datapack republishes its recipe to a newly joined client')
    await checkProbeRecipeForConnectedClient(async () => {
      const reloadResult = await rcon.command('reload')
      assert(reloadResult.includes('Reload complete'),
        'datapack reload completes through the live RCON command path')
    }, true,
    'datapack reload republishes its recipe to an already connected client')
    await checkProbeRecipeForNewClient(
      'RecipeReloadA', true,
      'enabled datapack recipe survives /reload and reaches a new client')

    // Mineflayer predicts air locally after its estimated dig time. Remove the
    // operator first (the only operator has no need to remain for later tests),
    // then verify the authoritative server block through read-only RCON state.
    const deopResult = await rcon.command(`deop ${first.username}`)
    assert(deopResult.includes(`De-opped ${first.username}`),
      'operator-only command fixture is closed before survival mining')
    const remoteObserverPosition = new Vec3(-9.5, -60, 0.5)
    const remoteObserverFloor = new Vec3(-10, -61, 0)
    await rcon.command(
      `setblock ${remoteObserverFloor.x} ${remoteObserverFloor.y} ` +
      `${remoteObserverFloor.z} minecraft:stone`)
    await rcon.command(
      `fill ${remoteObserverFloor.x} ${remoteObserverFloor.y + 1} ` +
      `${remoteObserverFloor.z} ${remoteObserverFloor.x} ` +
      `${remoteObserverFloor.y + 3} ${remoteObserverFloor.z} minecraft:air`)
    await waitForRconBlockState(
      rcon, remoteObserverFloor, 'minecraft:stone',
      'stable remote observer floor')
    await rcon.command(
      `tp ${first.username} ${remoteObserverPosition.x} ` +
      `${remoteObserverPosition.y} ${remoteObserverPosition.z}`)
    await waitUntil(() => first.entity.position.distanceTo(remoteObserverPosition) < 0.5,
      'the second player to move outside the chest-drop pickup bounds')
    const destroyFeet = interactionBot.entity.position.floored()
    // Keep the chest close enough for both clients to interact, but leave
    // room for the second miner to stand beside it without overlapping the
    // interacting player. Their spawn collision otherwise makes the later
    // item-pickup fixture depend on unstable player push-out movement.
    const destroyChestPos = new Vec3(
      destroyFeet.x + 3, destroyFeet.y - 1, destroyFeet.z)
    const dropSupport = new Vec3(
      destroyChestPos.x, destroyChestPos.y - 1, destroyChestPos.z)
    const overheadBlock = new Vec3(
      destroyChestPos.x, destroyChestPos.y + 6, destroyChestPos.z)
    await rcon.command(
      `setblock ${dropSupport.x} ${dropSupport.y} ` +
      `${dropSupport.z} minecraft:stone`)
    await waitUntil(() => interactionBot.blockAt(dropSupport)?.name === 'stone',
                    'a support block beneath the drop fixture')
    await rcon.command(
      `setblock ${overheadBlock.x} ${overheadBlock.y} ` +
      `${overheadBlock.z} minecraft:stone`)
    await waitUntil(() => interactionBot.blockAt(overheadBlock)?.name === 'stone',
                    'an overhead block to create a deterministic item-ground regression')
    await rcon.command(
      `setblock ${destroyChestPos.x} ${destroyChestPos.y} ${destroyChestPos.z} minecraft:chest`)
    const destroyChest = await waitUntil(() => {
      const block = interactionBot.blockAt(destroyChestPos)
      return block && block.name === 'chest' ? block : null
    }, 'destroy-test chest to arrive at the client')
    try {
      container = await withTimeout(interactionBot.openContainer(destroyChest), 15000,
                                    'destroy-test chest open')
    } catch (error) {
      console.error(`destroy-test diagnostics: player=${interactionBot.entity.position}, ` +
        `block=${destroyChest.name} at ${destroyChest.position}, ` +
        `window=${interactionBot.currentWindow?.id ?? 'none'}`)
      console.error(`server output before open failure:\n${child.output}`)
      throw error
    }
    await depositOneWithAuthoritativeClicks(
      interactionBot, container, diamondId, stateIdsByWindow)
    try {
      await waitUntil(() => countItemsInWindowInventory(container, 'diamond') === 2 &&
        container.containerItems().some(
          item => item.name === 'diamond' && item.count === 1),
      'destroy-test chest inventory to receive a diamond')
    } catch (error) {
      console.error('deposit diagnostics: ' + JSON.stringify({
        inventory: interactionBot.inventory.items().map(
          item => [item.name, item.count, item.slot]),
        window: {
          id: container.id,
          stateId: container.stateId,
          items: container.containerItems().map(
            item => [item.name, item.count, item.slot])
        }
      }))
      console.error(`menu wire trace:\n${menuWireTrace.join('\n')}`)
      throw error
    }
    const storedDiamond = container.containerItems().find(
      item => item.name === 'diamond' && item.count === 1)
    assert(storedDiamond,
      'the break-race fixture has exactly one diamond in the open chest')
    const minerPosition = new Vec3(
      destroyChestPos.x - 0.5, destroyChestPos.y + 1,
      destroyChestPos.z + 0.5)
    await rcon.command(
      `tp ${first.username} ${minerPosition.x} ${minerPosition.y} ` +
      `${minerPosition.z}`)
    await waitUntil(() => first.entity.position.distanceTo(minerPosition) < 0.5,
      'a second player to reach the chest while its GUI remains open')
    const minerChest = await waitUntil(() => {
      const block = first.blockAt(destroyChestPos)
      return block && block.name === 'chest' ? block : null
    }, 'the open-container chest to reach the mining client')
    let raceClickEnqueued = false
    let miningStartedResolve
    const miningStarted = new Promise(resolve => {
      miningStartedResolve = resolve
    })
    const windowLifecycleTrace = []
    const rememberWindowLifecycle = (name, packet) => {
      windowLifecycleTrace.push(`${name} window=${packet.windowId}`)
      if (windowLifecycleTrace.length > 12) windowLifecycleTrace.shift()
    }
    interactionBot._client.on('open_window', packet =>
      rememberWindowLifecycle('open_window', packet))
    interactionBot._client.on('close_window', packet =>
      rememberWindowLifecycle('close_window', packet))
    const originalMinerWrite = first._client.write
    first._client.write = function (name, packet, ...args) {
      const result = originalMinerWrite.call(this, name, packet, ...args)
      if (name === 'block_dig' && packet.status === 0 &&
          packet.location?.x === destroyChestPos.x &&
          packet.location?.y === destroyChestPos.y &&
          packet.location?.z === destroyChestPos.z)
        miningStartedResolve()
      return result
    }
    const originalObserverWrite = interactionBot._client.write
    interactionBot._client.write = function (name, packet, ...args) {
      const isRaceClick = name === 'window_click' &&
        packet.windowId === container.id
      const result = originalObserverWrite.call(this, name, packet, ...args)
      if (isRaceClick) raceClickEnqueued = true
      return result
    }
    try {
      const mining = withTimeout(first.dig(minerChest), 45000,
                                 'second player mining an open chest')
      await withTimeout(miningStarted, 5000,
                        'second client START packet for the chest')
      assert(interactionBot.currentWindow?.id === container.id,
        'the second client still has the chest GUI open at mining START')
      const concurrentMenuClick = (async () => {
        // Send a normal Mineflayer shift-click immediately after the miner's
        // real START packet. The two independent client sessions can then race
        // at the server: the click may commit first or be rejected after the
        // block entity is removed, but either ordering must conserve items.
        try {
          await withTimeout(
            interactionBot.clickWindow(storedDiamond.slot, 0, 1), 10000,
            'concurrent open-chest inventory click')
        } catch (error) {
          if (!raceClickEnqueued) {
            throw new Error(
              'concurrent chest click failed before its packet was enqueued',
              { cause: error })
          }
          // A correct server may close/reject this stale window after the
          // competing block break. Inventory conservation is the oracle.
        }
      })()
      await Promise.all([mining, concurrentMenuClick])
      await waitForRconBlockState(
        rcon, destroyChestPos, 'minecraft:air',
        'server-authoritative removal of the player-mined chest')
      assert(raceClickEnqueued,
        'a second-client window-click packet was enqueued during chest destruction')
    } catch (error) {
      console.error(`server-authoritative chest break was not observed; ` +
                    `player=${interactionBot.entity.position}, ` +
                    `cachedBlock=${interactionBot.blockAt(destroyChestPos)?.name}, ` +
                    `openWindow=${interactionBot.currentWindow?.id ?? 'none'}`)
      console.error(`window lifecycle: ${windowLifecycleTrace.join(', ')}`)
      console.error(`menu wire trace:\n${menuWireTrace.join('\n')}`)
      console.error(`server output before chest-break failure:\n${child.output.slice(-6000)}`)
      throw error
    } finally {
      first._client.write = originalMinerWrite
      interactionBot._client.write = originalObserverWrite
    }
    try {
      await waitUntil(() =>
        countItems(interactionBot.inventory.items(), 'diamond') +
          countItems(first.inventory.items(), 'diamond') === 3,
      'exactly one chest diamond to be transferred or dropped and picked up')
    } catch (error) {
      const nearbyItems = Object.values(interactionBot.entities)
        .filter(entity => entity.name === 'item')
        .map(entity => ({
          id: entity.id,
          position: entity.position,
          metadata: entity.metadata
        }))
      console.error('pickup diagnostics: ' + JSON.stringify({
        player: interactionBot.entity.position,
        chest: destroyChestPos,
        inventory: interactionBot.inventory.items().map(
          item => [item.name, item.count, item.slot]),
        nearbyItems
      }))
      console.error('server output before chest-drop pickup failure:\n' +
                    child.output.slice(-6000))
      throw error
    }
    if (interactionBot.currentWindow) {
      container.close()
      await waitUntil(() => !interactionBot.currentWindow,
        'stale chest GUI to close after its block entity was removed')
    }
    container = null
    // Move the miner away before the next item-drop fixture. Otherwise it can
    // collect the shulker spawned near the interacting client, making the
    // test depend on which player's pickup tick wins.
    await rcon.command(
      `tp ${first.username} ${remoteObserverPosition.x} ` +
      `${remoteObserverPosition.y} ${remoteObserverPosition.z}`)
    await waitUntil(() =>
      first.entity.position.distanceTo(remoteObserverPosition) < 0.5,
    'the miner to leave the following shulker pickup fixture')

    const dropRuleFeet = interactionBot.entity.position.floored()
    const suppressedDropPos = new Vec3(
      dropRuleFeet.x + 6, dropRuleFeet.y - 1, dropRuleFeet.z)
    const suppressedDropEvents = []
    const recordSuppressedDrop = entity => {
      if (entity?.name !== 'item' || !entity.position) return
      if (Math.abs(entity.position.x - (suppressedDropPos.x + 0.5)) > 1.5 ||
          Math.abs(entity.position.z - (suppressedDropPos.z + 0.5)) > 1.5 ||
          Math.abs(entity.position.y - suppressedDropPos.y) > 3)
        return
      suppressedDropEvents.push(entity.id)
    }
    interactionBot.on('entitySpawn', recordSuppressedDrop)
    interactionBot.on('itemDrop', recordSuppressedDrop)
    try {
      await rcon.command(
        `setblock ${suppressedDropPos.x} ${suppressedDropPos.y} ` +
        `${suppressedDropPos.z} minecraft:stone`)
      await waitForRconBlockState(
        rcon, suppressedDropPos, 'minecraft:stone',
        'doTileDrops fixture block to exist on the server')
      await rcon.command('gamerule doTileDrops false')
      const gameruleResult = await rcon.command('gamerule doTileDrops')
      assert(gameruleResult.includes('doTileDrops = false'),
        'server accepts doTileDrops=false before the destroy regression')
      const suppressedDestroy = await rcon.command(
        `fill ${suppressedDropPos.x} ${suppressedDropPos.y} ` +
        `${suppressedDropPos.z} ${suppressedDropPos.x} ` +
        `${suppressedDropPos.y} ${suppressedDropPos.z} minecraft:air destroy`)
      assert(suppressedDestroy.includes('Filled 1 blocks'),
        '/fill destroy still removes blocks when doTileDrops is false')
      await waitForRconBlockState(
        rcon, suppressedDropPos, 'minecraft:air',
        '/fill destroy block removal with doTileDrops=false')
      await sleep(1000)
      assert(suppressedDropEvents.length === 0,
        '/fill destroy does not spawn item entities when doTileDrops is false')
    } finally {
      interactionBot.removeListener('entitySpawn', recordSuppressedDrop)
      interactionBot.removeListener('itemDrop', recordSuppressedDrop)
      await rcon.command('gamerule doTileDrops true').catch(() => {})
    }

    for (const [offset, blockName, tableKind] of [
      [8, 'stone', 'valid-empty'],
      [9, 'cobblestone', 'unsupported-but-fallback-suppressed']
    ]) {
      const emptyLootPos = new Vec3(
        suppressedDropPos.x + offset, suppressedDropPos.y,
        suppressedDropPos.z)
      const observedEmptyLootDrops = []
      const recordEmptyLootDrop = entity => {
        if (entity?.name !== 'item' || !entity.position) return
        if (Math.abs(entity.position.x - (emptyLootPos.x + 0.5)) > 1.5 ||
            Math.abs(entity.position.z - (emptyLootPos.z + 0.5)) > 1.5 ||
            Math.abs(entity.position.y - emptyLootPos.y) > 3)
          return
        observedEmptyLootDrops.push(entity.id)
      }
      interactionBot.on('entitySpawn', recordEmptyLootDrop)
      interactionBot.on('itemDrop', recordEmptyLootDrop)
      try {
        await rcon.command(
          `setblock ${emptyLootPos.x} ${emptyLootPos.y} ` +
          `${emptyLootPos.z} minecraft:${blockName}`)
        await waitForRconBlockState(
          rcon, emptyLootPos, `minecraft:${blockName}`,
          `${blockName} fixture for empty /fill loot result`)
        const emptyLootDestroy = await rcon.command(
          `fill ${emptyLootPos.x} ${emptyLootPos.y} ${emptyLootPos.z} ` +
          `${emptyLootPos.x} ${emptyLootPos.y} ${emptyLootPos.z} ` +
          'minecraft:air destroy')
        assert(emptyLootDestroy.includes('Filled 1 blocks'),
          `/fill destroy removes ${blockName} with an ${tableKind} table`)
        await waitForRconBlockState(
          rcon, emptyLootPos, 'minecraft:air',
          `/fill destroy with ${tableKind} loot`)
        await sleep(500)
        assert(observedEmptyLootDrops.length === 0,
          `/fill destroy honors the ${tableKind} loot instead of default-dropping ${blockName}`)
      } finally {
        interactionBot.removeListener('entitySpawn', recordEmptyLootDrop)
        interactionBot.removeListener('itemDrop', recordEmptyLootDrop)
      }
    }

    await rcon.command(
      `setblock ${overheadBlock.x} ${overheadBlock.y} ` +
      `${overheadBlock.z} minecraft:air`)
    const shulkerFeet = interactionBot.entity.position.floored()
    const shulkerPos = new Vec3(
      shulkerFeet.x + 1, shulkerFeet.y - 1, shulkerFeet.z)
    await rcon.command(
      `setblock ${shulkerPos.x} ${shulkerPos.y} ${shulkerPos.z} minecraft:red_shulker_box`)
    const shulkerBlock = await waitUntil(() => {
      const block = interactionBot.blockAt(shulkerPos)
      return block && block.name === 'red_shulker_box' ? block : null
    }, 'test shulker box to arrive at the client')
    try {
      container = await withTimeout(interactionBot.openContainer(shulkerBlock), 15000,
                                    'shulker box open')
    } catch (error) {
      console.error(`shulker diagnostics: player=${interactionBot.entity.position}, ` +
        `block=${shulkerBlock.name} at ${shulkerBlock.position}, ` +
        `window=${interactionBot.currentWindow?.id ?? 'none'}`)
      console.error(`server output before shulker open failure:\n${child.output}`)
      throw error
    }
    await depositOneWithAuthoritativeClicks(
      interactionBot, container, diamondId, stateIdsByWindow)
    await waitUntil(() => countItemsInWindowInventory(container, 'diamond') === 2 &&
      container.containerItems().some(
        item => item.name === 'diamond' && item.count === 1),
    'shulker inventory item to arrive before destroy')
    container.close()
    container = null
    const shulkerDestroy = await rcon.command(
      `fill ${shulkerPos.x} ${shulkerPos.y} ${shulkerPos.z} ` +
      `${shulkerPos.x} ${shulkerPos.y} ${shulkerPos.z} minecraft:air destroy`)
    assert(shulkerDestroy.includes('Filled 1 blocks'),
      '/fill destroy handles a shulker box with contents')
    await waitUntil(() => interactionBot.blockAt(shulkerPos)?.name === 'air',
                    'destroyed shulker box update to reach the client')
    // The destroyed box was the client's temporary standing surface. Give it
    // a stable floor one block below before teleporting back for pickup; an
    // unsupported target lets Mineflayer gravity move the bot away before the
    // item pickup tick, making this probe depend on packet/tick scheduling.
    const shulkerPickupSupport = new Vec3(
      shulkerPos.x, shulkerPos.y - 1, shulkerPos.z)
    await rcon.command(
      `setblock ${shulkerPickupSupport.x} ${shulkerPickupSupport.y} ` +
      `${shulkerPickupSupport.z} minecraft:stone`)
    await waitForRconBlockState(
      rcon, shulkerPickupSupport, 'minecraft:stone',
      'stable floor below the shulker pickup point')
    const shulkerPickupPosition = new Vec3(
      shulkerPos.x + 0.5, shulkerPos.y, shulkerPos.z + 0.5)
    const shulkerTeleportResult = await rcon.command(
      `tp ${interactionBot.username} ${shulkerPickupPosition.x} ` +
      `${shulkerPickupPosition.y} ${shulkerPickupPosition.z}`)
    try {
      await waitUntil(() =>
        interactionBot.entity.position.distanceTo(shulkerPickupPosition) < 0.5,
      'the client to stand within pickup range of the destroyed shulker')
    } catch (error) {
      console.error('shulker pickup teleport diagnostics: ' + JSON.stringify({
        commandResult: shulkerTeleportResult,
        expected: shulkerPickupPosition,
        actual: interactionBot.entity.position,
        observer: first.entities[interactionBot.entity.id]?.position,
        controls: interactionBot.controlState
      }))
      console.error(`interaction position wire trace:\n${interactionPositionTrace.join('\n')}`)
      console.error(`server output before shulker teleport failure:\n${persistenceLogSummary(child.output)}`)
      throw error
    }
    let recoveredShulker
    try {
      recoveredShulker = await waitUntil(() =>
        interactionBot.inventory.items().find(item =>
          item.name === 'red_shulker_box'),
      'contents-preserving red shulker drop to be picked up')
    } catch (error) {
      const nearbyItems = Object.values(interactionBot.entities)
        .filter(entity => entity.name === 'item')
        .map(entity => ({
          id: entity.id,
          position: entity.position,
          metadata: entity.metadata
        }))
      console.error('shulker pickup diagnostics: ' + JSON.stringify({
        player: interactionBot.entity.position,
        controls: interactionBot.controlState,
        otherPlayerInventory: first.inventory.items().map(
          item => [item.name, item.count, item.slot]),
        droppedItems: Object.values(interactionBot.entities)
          .filter(entity => entity.name === 'item')
          .map(entity => ({position: entity.position, metadata: entity.metadata})),
        inventory: interactionBot.inventory.items().map(
          item => [item.name, item.count, item.slot]),
        nearbyItems
      }))
      console.error(`server output before shulker pickup failure:\n${persistenceLogSummary(child.output)}`)
      throw error
    }
    const packedContents = recoveredShulker.componentMap
      ?.get('container')?.data?.contents
    const retainedDiamond = Array.isArray(packedContents) &&
      packedContents.some(item => item && item.itemId === diamondId &&
        item.itemCount === 1)
    if (!retainedDiamond)
      console.error('recovered shulker components: ' +
        JSON.stringify(recoveredShulker.components))
    assert(retainedDiamond,
      '/fill destroy preserves red shulker color and its diamond container contents')

    const shulkerPlaceStandSupport = new Vec3(
      shulkerPos.x + 2, shulkerPos.y - 1, shulkerPos.z)
    const shulkerPlaceStand = new Vec3(
      shulkerPlaceStandSupport.x + 0.5, shulkerPos.y,
      shulkerPlaceStandSupport.z + 0.5)
    await rcon.command(
      `setblock ${shulkerPlaceStandSupport.x} ${shulkerPlaceStandSupport.y} ` +
      `${shulkerPlaceStandSupport.z} minecraft:stone`)
    await rcon.command(
      `tp ${interactionBot.username} ${shulkerPlaceStand.x} ` +
      `${shulkerPlaceStand.y} ${shulkerPlaceStand.z}`)
    await waitUntil(() =>
      interactionBot.entity.position.distanceTo(shulkerPlaceStand) < 0.5,
    'the player to move clear of the packed shulker placement target')
    await interactionBot.equip(recoveredShulker, 'hand')
    const shulkerPlacementSupport = await waitUntil(() => {
      const block = interactionBot.blockAt(shulkerPickupSupport)
      return block && block.name === 'stone' ? block : null
    }, 'support block for ordinary shulker placement to reach the client')
    await withTimeout(
      interactionBot.placeBlock(shulkerPlacementSupport, new Vec3(0, 1, 0)),
      15000, 'ordinary player placement of a packed shulker box')
    const playerPlacedShulker = await waitUntil(() => {
      const block = interactionBot.blockAt(shulkerPos)
      return block && block.name === 'red_shulker_box' ? block : null
    }, 'player-placed shulker box to arrive at the client')
    await waitUntil(() => !interactionBot.inventory.items().some(
      item => item.name === 'red_shulker_box'),
    'successful packed-shulker placement to consume its item after admission')
    container = await withTimeout(
      interactionBot.openContainer(playerPlacedShulker), 15000,
      'reopening a player-placed packed shulker')
    await waitUntil(() => container.containerItems().some(
      item => item.name === 'diamond' && item.count === 1),
    'packed shulker contents to survive ordinary player placement and reopen')
    const playerPlacementRetainedContents = container.containerItems().some(
      item => item.name === 'diamond' && item.count === 1)
    container.close()
    await waitUntil(() => !interactionBot.currentWindow,
                    'player-placed shulker window to close before destroy')
    container = null
    assert(playerPlacementRetainedContents,
      'ordinary Mineflayer placement restores packed shulker contents before item consumption')
    const playerPlacedShulkerDestroy = await rcon.command(
      `fill ${shulkerPos.x} ${shulkerPos.y} ${shulkerPos.z} ` +
      `${shulkerPos.x} ${shulkerPos.y} ${shulkerPos.z} minecraft:air destroy`)
    assert(playerPlacedShulkerDestroy.includes('Filled 1 blocks'),
      'player-placed shulker remains safely destroyable after reopening')
    await waitUntil(() => interactionBot.blockAt(shulkerPos)?.name === 'air',
                    'player-placed shulker destroy update to reach the client')
    await rcon.command(
      `tp ${interactionBot.username} ${shulkerPickupPosition.x} ` +
      `${shulkerPickupPosition.y} ${shulkerPickupPosition.z}`)
    await waitUntil(() =>
      interactionBot.entity.position.distanceTo(shulkerPickupPosition) < 0.5,
    'the player to return to pick up the repacked shulker')
    recoveredShulker = await waitUntil(() =>
      interactionBot.inventory.items().find(item =>
        item.name === 'red_shulker_box'),
    'repacked red shulker to be picked up after player placement')

    const dispenserFeet = interactionBot.entity.position.floored()
    const dispenserPos = new Vec3(
      dispenserFeet.x + 2, dispenserFeet.y - 1, dispenserFeet.z)
    const dispensedShulkerPos = new Vec3(
      dispenserPos.x + 1, dispenserPos.y, dispenserPos.z)
    const dispensedShulkerBelow = new Vec3(
      dispensedShulkerPos.x, dispensedShulkerPos.y - 1,
      dispensedShulkerPos.z)
    const dispenserPowerPos = new Vec3(
      dispenserPos.x, dispenserPos.y, dispenserPos.z + 1)
    const destinationAirResult = await rcon.command(
      `setblock ${dispensedShulkerPos.x} ${dispensedShulkerPos.y} ` +
      `${dispensedShulkerPos.z} minecraft:cave_air`)
    const supportAirResult = await rcon.command(
      `setblock ${dispensedShulkerBelow.x} ${dispensedShulkerBelow.y} ` +
      `${dispensedShulkerBelow.z} minecraft:cave_air`)
    try {
      await waitForRconBlockState(
        rcon, dispensedShulkerPos, 'minecraft:cave_air',
        'cave-air dispenser destination before power')
      await waitForRconBlockState(
        rcon, dispensedShulkerBelow, 'minecraft:cave_air',
        'cave-air support position before power')
    } catch (error) {
      console.error('dispenser cave-air diagnostics: ' + JSON.stringify({
        player: interactionBot.entity.position,
        dispenser: dispenserPos,
        destination: dispensedShulkerPos,
        destinationSet: destinationAirResult,
        destinationObserved: await rcon.command(
          `data get block ${dispensedShulkerPos.x} ` +
          `${dispensedShulkerPos.y} ${dispensedShulkerPos.z}`),
        support: dispensedShulkerBelow,
        supportSet: supportAirResult,
        supportObserved: await rcon.command(
          `data get block ${dispensedShulkerBelow.x} ` +
          `${dispensedShulkerBelow.y} ${dispensedShulkerBelow.z}`)
      }))
      console.error(`server output before dispenser cave-air failure:\n${persistenceLogSummary(child.output)}`)
      throw error
    }
    await rcon.command(
      `setblock ${dispenserPowerPos.x} ${dispenserPowerPos.y} ` +
      `${dispenserPowerPos.z} minecraft:air`)
    await rcon.command(
      `setblock ${dispenserPos.x} ${dispenserPos.y} ${dispenserPos.z} ` +
      `minecraft:dispenser[facing=east]`)
    const dispenser = await waitUntil(() => {
      const block = interactionBot.blockAt(dispenserPos)
      return block && block.name === 'dispenser' ? block : null
    }, 'dispenser to arrive before inserting the filled shulker')
    container = await withTimeout(interactionBot.openContainer(dispenser), 15000,
                                  'dispenser inventory open')
    const redShulkerId = interactionBot.registry.itemsByName.red_shulker_box.id
    await depositOneWithAuthoritativeClicks(
      interactionBot, container, redShulkerId, stateIdsByWindow)
    await waitUntil(() => container.containerItems().some(
      item => item.name === 'red_shulker_box'),
    'filled red shulker to enter the dispenser inventory')
    container.close()
    await waitUntil(() => !interactionBot.currentWindow,
                    'dispenser window to close before powering it')
    container = null
    await rcon.command(
      `setblock ${dispenserPowerPos.x} ${dispenserPowerPos.y} ` +
      `${dispenserPowerPos.z} minecraft:redstone_block`)
    await waitUntil(
      () => interactionBot.blockAt(dispensedShulkerPos)?.name ===
        'red_shulker_box',
      'powered dispenser to place the filled red shulker', 25000)
    const dispensedShulker = await waitUntil(() => {
      const block = interactionBot.blockAt(dispensedShulkerPos)
      return block && block.name === 'red_shulker_box' ? block : null
    }, 'dispensed shulker to arrive in the client world')
    assert(dispensedShulker.getProperties().facing === 'east',
      'dispenser treats cave air as replaceable space and as an empty support position')
    container = await withTimeout(
      interactionBot.openContainer(dispensedShulker), 15000,
      'dispensed shulker inventory open')
    await waitUntil(() => container.containerItems().some(
      item => item.name === 'diamond' && item.count === 1),
    'filled shulker contents to survive dispenser placement')
    container.close()
    await waitUntil(() => !interactionBot.currentWindow,
                    'dispensed shulker window to close')
    container = null

    const unloadedChestPos = new Vec3(512, -50, 512)
    await rcon.command(
      `setblock ${unloadedChestPos.x} ${unloadedChestPos.y - 1} ` +
      `${unloadedChestPos.z} minecraft:stone`)
    await rcon.command(
      `setblock ${unloadedChestPos.x} ${unloadedChestPos.y} ` +
      `${unloadedChestPos.z} minecraft:chest`)
    await rcon.command(
      `tp CppfmProbeA ${unloadedChestPos.x} ` +
      `${unloadedChestPos.y + 1} ${unloadedChestPos.z}`)
    const unloadedChest = await waitUntil(() => {
      const block = first.blockAt(unloadedChestPos)
      return block && block.name === 'chest' ? block : null
    }, 'far chest to load before saving the unloaded-chunk fixture')
    container = await withTimeout(first.openContainer(unloadedChest), 15000,
                                  'far unloaded-test chest open')
    container.close()
    await waitUntil(() => !first.currentWindow,
                    'far chest window to close before seeding its inventory')
    container = null
    const seededFarInventory = await rcon.command(
      `item replace block ${unloadedChestPos.x} ${unloadedChestPos.y} ` +
      `${unloadedChestPos.z} container.0 with minecraft:diamond 1`)
    assert(seededFarInventory.includes('Replaced block'),
      'server command seeds the far chest inventory before restart')
    container = await withTimeout(first.openContainer(unloadedChest), 15000,
                                  'seeded far chest open')
    await waitUntil(() => container.containerItems().some(
      item => item.name === 'diamond' && item.count === 1),
    'far chest inventory to receive its persisted fixture item')
    container.close()
    await waitUntil(() => !first.currentWindow,
                    'far chest window to close before server restart')
    container = null
    await rcon.command(`give ${first.username} minecraft:emerald 1`)
    await waitUntil(() => countItems(first.inventory.items(), 'emerald') === 1,
                    'player inventory persistence marker to be delivered')
    await rcon.command(`gamemode survival ${first.username}`)
    const healthBeforeSave = first.health
    await rcon.command(`damage ${first.username} 4 minecraft:generic`)
    await waitUntil(
      () => first.health < healthBeforeSave ? first.health : null,
      'player damage fixture to update health before persistence', 10000)
    await rcon.command(`gamemode creative ${first.username}`)
    await rcon.command('defaultgamemode creative')
    await rcon.command('defaultgamemode survival')
    await sleep(4000)
    const persistedHealth = first.health
    await stopBot(second)
    second = null
    await stopBot(first)
    first = null
    rcon.close()
    rcon = null
    previousServerOutput = child.output
    await stopOwnedServer(child, true)
    child = null

    const restarted = await startReadyProbeServer(
      binaryPath, root, rconPassword, 'force-gamemode=true\n',
      owned => { child = owned })
    child = restarted.child
    rcon = restarted.rcon
    const unloadedFeatureResult = await rcon.command(
      `place feature minecraft:oak ${unloadedChestPos.x} ` +
      `${unloadedChestPos.y} ${unloadedChestPos.z}`)
    assert(unloadedFeatureResult.includes(
      'no vanilla configured-feature registry is loaded'),
    '/place feature fails closed before touching an unloaded block-entity chunk')

    persistedInventoryProbe = await connectBot('CppfmProbeA', restarted.gamePort)
    assert(persistedInventoryProbe.game.gameMode === 'survival',
      'force-gamemode applies the persisted world default over a saved player mode')
    await waitUntil(
      () => countItems(persistedInventoryProbe.inventory.items(), 'emerald') === 1,
      'saved player inventory marker to survive reconnect')
    assert(countItems(persistedInventoryProbe.inventory.items(), 'emerald') === 1,
      'rejoining with the same offline UUID preserves the saved inventory')
    assert(Math.abs(persistedInventoryProbe.health - persistedHealth) < 0.01,
      `rejoining after restart restores saved player health instead of fresh defaults (saved=${persistedHealth}, joined=${persistedInventoryProbe.health})`)
    await stopBot(persistedInventoryProbe)
    persistedInventoryProbe = null
    await waitForRconPlayerAbsent(rcon, 'CppfmProbeA')

    const playerDataPath = path.join(
      root, 'world', 'playerdata',
      `${offlinePlayerUuidHex('CppfmProbeA')}.dat`)
    const playerData = await fs.readFile(playerDataPath)
    const parsedPlayerData = await nbt.parse(playerData)
    const playerRoot = parsedPlayerData.parsed?.value
    if (!playerRoot || typeof playerRoot !== 'object')
      throw new Error('could not inspect the player root NBT fixture')
    const attachmentMarker = 'cppfm-root-nbt-preservation'
    playerRoot['example:attachment'] = {
      type: 'compound',
      value: { marker: { type: 'string', value: attachmentMarker } }
    }
    await fs.writeFile(playerDataPath,
      nbt.writeUncompressed(parsedPlayerData.parsed, parsedPlayerData.type))
    const attachmentProbe = await connectBot(
      'CppfmProbeA', restarted.gamePort)
    await stopBot(attachmentProbe)
    await waitForRconPlayerAbsent(rcon, 'CppfmProbeA')
    const playerDataAfterAttachmentSave = await fs.readFile(playerDataPath)
    const attachmentRoundTrip = await nbt.parse(playerDataAfterAttachmentSave)
    const preservedAttachment = attachmentRoundTrip.parsed?.value
      ?.["example:attachment"]?.value?.marker?.value
    assert(preservedAttachment === attachmentMarker,
      'disconnect save retains unknown mod-owned player root NBT')

    const savedInventory = attachmentRoundTrip.parsed?.value?.Inventory
    if (savedInventory?.type !== 'list' ||
        savedInventory.value?.type !== 'compound' ||
        !Array.isArray(savedInventory.value.value)) {
      throw new Error('could not inspect the Mineflayer player inventory fixture')
    }
    const occupiedSlots = new Set(savedInventory.value.value
      .map(item => item.Slot?.value)
      .filter(Number.isInteger))
    const unusedSlot = Array.from({ length: 46 }, (_, slot) => slot)
      .find(slot => !occupiedSlots.has(slot))
    if (unusedSlot === undefined)
      throw new Error('no free player inventory slot for unknown-item probe')
    savedInventory.value.value.push({
      id: { type: 'string', value: 'example:modded_item' },
      Count: { type: 'byte', value: 1 },
      Slot: { type: 'byte', value: unusedSlot }
    })
    await fs.writeFile(playerDataPath,
      nbt.writeUncompressed(attachmentRoundTrip.parsed,
                            attachmentRoundTrip.type))
    const playerDataWithUnknownItem = await fs.readFile(playerDataPath)

    const incompatibleBot = mineflayer.createBot({
      host: '127.0.0.1',
      port: restarted.gamePort,
      username: 'CppfmProbeA',
      version: '1.21.4',
      auth: 'offline'
    })
    const [kickReason] = await withTimeout(
      once(incompatibleBot, 'kicked'), 15000,
      'unsupported player item to reject login without erasing data')
    await stopBot(incompatibleBot)
    const kickText = JSON.stringify(kickReason).toLowerCase()
    assert(kickText.includes('cannot load') && kickText.includes('preserved'),
      'login explains that an unregistered saved item is unsupported and preserved')
    const retainedPlayerData = await fs.readFile(playerDataPath)
    let unsupportedSaveQuarantined = false
    try {
      await fs.access(`${playerDataPath}.corrupt`)
      unsupportedSaveQuarantined = true
    } catch (_) {}
    assert(retainedPlayerData.equals(playerDataWithUnknownItem) &&
           !unsupportedSaveQuarantined,
      'rejected Mineflayer login leaves the active modded player save byte-identical and unquarantined')

    const enderProbeUsername = 'CppfmEnderProbe'
    const enderProbePath = path.join(
      root, 'world', 'playerdata',
      `${offlinePlayerUuidHex(enderProbeUsername)}.dat`)
    const enderProbeDocument = await nbt.parse(playerData)
    const enderProbeRoot = enderProbeDocument.parsed?.value
    if (!enderProbeRoot || typeof enderProbeRoot !== 'object')
      throw new Error('could not inspect the EnderItems NBT fixture')
    enderProbeRoot.EnderItems = {
      type: 'list',
      value: {
        type: 'compound',
        value: [{
          id: { type: 'string', value: 'minecraft:diamond' },
          Count: { type: 'byte', value: 1 },
          Slot: { type: 'byte', value: 0 },
          components: {
            type: 'compound',
            value: {
              'example:opaque_component': {
                type: 'string', value: 'must remain untouched'
              }
            }
          }
        }]
      }
    }
    await fs.writeFile(enderProbePath,
      nbt.writeUncompressed(enderProbeDocument.parsed,
                            enderProbeDocument.type))
    const enderPlayerDataBeforeLogin = await fs.readFile(enderProbePath)
    unsupportedEnderProbe = mineflayer.createBot({
      host: '127.0.0.1',
      port: restarted.gamePort,
      username: enderProbeUsername,
      version: '1.21.4',
      auth: 'offline'
    })
    const [enderKickReason] = await withTimeout(
      once(unsupportedEnderProbe, 'kicked'), 15000,
      'opaque EnderItems to reject login before an unusable ender chest opens')
    await stopBot(unsupportedEnderProbe)
    unsupportedEnderProbe = null
    const enderKickText = JSON.stringify(enderKickReason).toLowerCase()
    assert(enderKickText.includes('cannot encode') &&
           enderKickText.includes('preserved'),
      'login rejects EnderItems this server cannot serialize and explains preservation')
    const enderPlayerDataAfterLogin = await fs.readFile(enderProbePath)
    let unsupportedEnderSaveQuarantined = false
    try {
      await fs.access(`${enderProbePath}.corrupt`)
      unsupportedEnderSaveQuarantined = true
    } catch (_) {}
    assert(enderPlayerDataAfterLogin.equals(enderPlayerDataBeforeLogin) &&
           !unsupportedEnderSaveQuarantined,
      'opaque EnderItems remain byte-identical and are not quarantined after rejected login')

    let reloadedSignChunkRecord = null
    const rememberReloadedSignChunk = packet => {
      if (packet.x !== (signPos.x >> 4) ||
          packet.z !== (signPos.z >> 4)) return
      const localX = ((signPos.x % 16) + 16) % 16
      const localZ = ((signPos.z % 16) + 16) % 16
      reloadedSignChunkRecord = (packet.blockEntities || []).find(record =>
        record.x === localX && record.z === localZ &&
        record.y === signPos.y && record.type === 7) ||
        reloadedSignChunkRecord
    }
    first = await connectBot('CppfmProbeReload', restarted.gamePort, bot => {
      // Observe packets from the beginning of login, before Mineflayer's spawn
      // promise resolves and it may already have received the spawn chunk.
      bot._client.on('map_chunk', rememberReloadedSignChunk)
    })
    assert(first.game.gameMode === 'survival',
      'a new player after restart receives the persisted default game mode')
    try {
      // Force the sign's chunk through the normal player streaming path after
      // restart; a stale Mineflayer block cache is not persistence evidence.
      await rcon.command(
        `tp ${first.username} ${signPos.x + 0.5} ${signPos.y + 2} ${signPos.z + 0.5}`)
      await waitUntil(() => first.blockAt(signPos)?.name === 'oak_sign',
        'persisted sign block to reload after restart')
      const persistedSignLines = await waitUntil(() => {
        const lines = signTextLinesFromNbt(
          reloadedSignChunkRecord?.nbtData)
        return lines && lines[0] === signLines[0] ? lines : null
      }, 'initial chunk packet to carry persisted sign text after restart')
      assert(persistedSignLines.every((line, index) => line === signLines[index]),
        'initial chunk block-entity records survive a complete server restart')
    } catch (error) {
      console.error('persisted-sign diagnostics: ' + JSON.stringify({
        position: signPos,
        player: first.entity?.position,
        block: first.blockAt(signPos)?.name,
        entity: first.blockAt(signPos)?.entity,
        chunkBlockEntityRecord: reloadedSignChunkRecord
      }))
      console.error(`persistence logs before restart:\n${persistenceLogSummary(previousServerOutput)}`)
      console.error(`persistence logs after restart:\n${persistenceLogSummary(child.output)}`)
      throw error
    } finally {
      first._client.removeListener('map_chunk', rememberReloadedSignChunk)
    }
    await rcon.command(
      `tp CppfmProbeReload ${unloadedChestPos.x} ` +
      `${unloadedChestPos.y + 1} ${unloadedChestPos.z}`)
    const reloadedChest = await waitUntil(() => {
      const block = first.blockAt(unloadedChestPos)
      return block && block.name === 'chest' ? block : null
    }, 'persisted chest block to reload after a fail-closed feature request')
    container = await withTimeout(first.openContainer(reloadedChest), 15000,
                                  'reloaded persisted chest open')
    try {
      await waitUntil(() => container.containerItems().some(
        item => item.name === 'diamond' && item.count === 1),
      'persisted chest inventory to survive a fail-closed feature request')
    } catch (error) {
      console.error('unloaded-chunk preservation diagnostics: ' + JSON.stringify({
        unloadedFeatureResult,
        block: first.blockAt(unloadedChestPos)?.name,
        window: container.containerItems().map(
          item => [item.name, item.count, item.slot])
      }))
      console.error(`server output before restart:\n${previousServerOutput}`)
      console.error(`server output after restart:\n${child.output}`)
      throw error
    }

    // Run the movement probe last. Mineflayer locally predicts movement and
    // can coast far beyond the first accepted packet; doing this before the
    // gameplay/persistence scenarios makes their fixtures depend on the
    // prediction tail instead of the server behavior under test.
    container.close()
    container = null
    second = await connectBot('CppfmMoveObs', restarted.gamePort)
    assert(first.entity && second.entity,
      'movement probe clients are connected after the persistence restart')
    const movementOrigin = new Vec3(544, -60, 544)
    const movementFloorY = movementOrigin.y - 2
    const movementMinX = movementOrigin.x - 12
    const movementMaxX = movementOrigin.x + 12
    const movementMinZ = movementOrigin.z - 16
    const movementMaxZ = movementOrigin.z + 96
    await rcon.command(
      `fill ${movementMinX} ${movementFloorY} ${movementMinZ} ` +
      `${movementMaxX} ${movementFloorY} ${movementMaxZ} minecraft:stone`)
    await rcon.command(
      `fill ${movementMinX} ${movementFloorY + 1} ${movementMinZ} ` +
      `${movementMaxX} ${movementOrigin.y + 2} ${movementMaxZ} minecraft:air`)
    const movementObserverPosition = new Vec3(
      movementOrigin.x + 0.5, movementFloorY + 1, movementOrigin.z + 0.5)
    const movementClientPosition = new Vec3(
      movementOrigin.x + 8.5, movementFloorY + 1, movementOrigin.z - 8.5)
    await rcon.command(
      `tp ${first.username} ${movementObserverPosition.x} ` +
      `${movementObserverPosition.y} ${movementObserverPosition.z}`)
    await rcon.command(
      `tp ${second.username} ${movementClientPosition.x} ` +
      `${movementClientPosition.y} ${movementClientPosition.z}`)
    await waitUntil(() =>
      first.entity.position.distanceTo(movementObserverPosition) < 0.35 &&
      second.entity.position.distanceTo(movementClientPosition) < 0.35,
    'movement probe clients to reach isolated positions')
    await waitUntil(
      () => first.entities[second.entity.id],
      'observer to track the Mineflayer movement-probe client')
    const finalMovementWireTrace = []
    const finalObserverMovementTrace = []
    const selfMovementPackets = []
    const originalMovementClientWrite = second._client.write.bind(second._client)
    second._client.write = (name, packet, ...args) => {
      if (name === 'position' || name === 'position_look') {
        finalMovementWireTrace.push(`out ${name} ${JSON.stringify(packet)}`)
        if (finalMovementWireTrace.length > 16)
          finalMovementWireTrace.shift()
      }
      return originalMovementClientWrite(name, packet, ...args)
    }
    for (const packetName of [
      'rel_entity_move', 'entity_move_look', 'entity_teleport'
    ]) {
      const captureMovement = (packet, trace) => {
        if (packet.entityId !== second.entity.id) return
        trace.push(`${packetName} ${JSON.stringify(packet)}`)
        if (trace.length > 24) trace.shift()
      }
      first._client.on(packetName,
        packet => captureMovement(packet, finalObserverMovementTrace))
      second._client.on(packetName,
        packet => captureMovement(packet, selfMovementPackets))
    }
    // The target client receives RCON teleports directly, but the observer
    // does not get a movement update until the target sends its next movement
    // packet. Use the confirmed server destination as the baseline rather
    // than the observer's intentionally stale pre-teleport entity cache.
    const movementBefore = movementClientPosition.clone()
    const movementStartedAt = Date.now()
    second.setControlState('forward', true)
    await withAbort(sleep(1000))
    second.clearControlStates()
    let movementAfter
    try {
      movementAfter = await waitUntil(() => {
        const observed = first.entities[second.entity.id]
        if (!observed) return false
        const moved = Math.hypot(
          observed.position.x - movementBefore.x,
          observed.position.z - movementBefore.z)
        const clientServerDelta = Math.hypot(
          observed.position.x - second.entity.position.x,
          observed.position.z - second.entity.position.z)
        return moved > 0.2 && clientServerDelta < 1.0
          ? observed.position.clone() : false
      }, 'observer to receive accepted Mineflayer movement', 10000)
    } catch (error) {
      console.error(`final movement wire trace:\n${finalMovementWireTrace.join('\n')}`)
      console.error(`final observer movement trace:\n${finalObserverMovementTrace.join('\n')}`)
      console.error(`final movement positions: before=${movementBefore}, ` +
        `observer=${first.entities[second.entity.id]?.position}, ` +
        `client=${second.entity.position}`)
      throw error
    }
    const movementDistance = Math.hypot(
      movementAfter.x - movementBefore.x,
      movementAfter.z - movementBefore.z)
    const finalMovementDelta = Math.hypot(
      movementAfter.x - second.entity.position.x,
      movementAfter.z - second.entity.position.z)
    assert(movementDistance > 0.2 && movementDistance < 6 &&
           finalMovementDelta < 1.0,
      `server accepts ordinary Mineflayer movement ` +
      `(${movementDistance.toFixed(2)} blocks, ` +
      `client/server delta=${finalMovementDelta.toFixed(2)}, ` +
      `observed=${movementBefore}->${movementAfter}, ` +
      `elapsed=${Date.now() - movementStartedAt}ms)`)
    assert(selfMovementPackets.length === 0,
      `player movement is not echoed back as entity-tracking packets ` +
      `(received=${selfMovementPackets.length})`)
  } catch (error) {
    primaryError = error
    throw error
  } finally {
    if (container) {
      try { container.close() } catch (_) {}
    }
    if (observerContainer) {
      try { observerContainer.close() } catch (_) {}
    }
    await stopBot(second)
    await stopBot(persistedInventoryProbe)
    await stopBot(unsupportedEnderProbe)
    await stopBot(first)
    if (rcon) rcon.close()
    let stopError = null
    try { await stopOwnedServer(child, true) }
    catch (error) { stopError = error }
    const serverExited = !child || child.pid == null ||
      child.exitCode !== null || child.signalCode !== null
    if (root && serverExited && !stopError &&
        process.env.CPPFM_MINEFLAYER_KEEP_WORLD === '1') {
      console.error(`preserved isolated Mineflayer world for inspection: ${root}`)
    } else if (root && serverExited && !stopError) {
      try { await fs.rm(root, { recursive: true, force: true }) }
      catch (error) { stopError ||= error }
    } else if (root) {
      console.error(`retaining isolated test world after incomplete cleanup: ${root}`)
    }
    if (stopError) {
      if (primaryError) {
        console.error(
          `owned-server cleanup also failed after the scenario error: ` +
          `${stopError.stack || stopError}`)
      } else {
        throw stopError
      }
    }
  }
  await runJvmCallbackProbe(binaryPath)
}

async function runJvmCallbackProbe(binary) {
  const buildRoot = path.dirname(binary)
  const classes = path.join(buildRoot, 'jvm', 'classes')
  const mods = path.join(buildRoot, 'jvm', 'fixture-mods')
  try {
    await Promise.all([fs.access(classes), fs.access(mods)])
  } catch (error) {
    throw new Error(
      `required JVM callback fixture artifacts are missing ` +
      `(classes: ${classes}; fixture mods: ${mods}): ${error.message}`)
  }

  let root
  let child
  let rconPort
  let rconPassword
  let bot
  let concurrentBot
  let rcon
  let primaryError = null
  try {
    root = await fs.mkdtemp(path.join(os.tmpdir(), 'cppfm-jvm-mineflayer-'))
    const [gamePort, reservedRconPort] = await reserveLoopbackPorts()
    rconPort = reservedRconPort
    rconPassword = randomBytes(16).toString('hex')
    await fs.writeFile(path.join(root, 'server.properties'),
      `rcon.port=${rconPort}\nrcon.password=${rconPassword}\n`)
    child = startOwnedServer(binary, root, gamePort, true)
    await waitForPort(child, gamePort, 'embedded JVM fixture server', 90000)
    await waitUntil(() => child.hasFixtureOutput('fixture POST_RELOAD_TICK'),
      'embedded Fabric fixture startup callbacks', 90000)
    const damageBridgeMarkers = [
      'fixture DAMAGE_BRIDGE_SETUP true',
      'fixture DAMAGE_BRIDGE_CANCEL true',
      'fixture DAMAGE_BRIDGE_NESTED_REJECT true',
      'fixture DAMAGE_BRIDGE_DEATH_VETO true',
      'fixture DAMAGE_BRIDGE_DEATH_ALLOW true'
    ]
    await waitUntil(() => damageBridgeMarkers.every(marker =>
      child.hasFixtureOutput(marker)),
    'embedded JVM native damage, recursive-callback, and death-gate contracts',
    15000)
    for (const marker of damageBridgeMarkers)
      assert(child.hasFixtureOutput(marker), marker)
    await waitUntil(() => child.hasFixtureOutput(
      'fixture WORLD_DIMENSION_SURFACES true'),
    'Fabric World wrappers to preserve all three dimension bounds', 10000)
    assert(child.hasFixtureOutput('fixture WORLD_DIMENSION_SURFACES true'),
      'Overworld, Nether, and End world/dimension surfaces agree')
    rcon = await waitForRcon(rconPort, rconPassword)
    const joinKickReason = await expectJoinRejected('JvmJoinKick', gamePort)
    const serializedJoinKickReason = JSON.stringify(joinKickReason)
    await waitUntil(() => child.hasFixtureOutput(
      'fixture JOIN_DISCONNECT_REQUESTED'),
    'Fabric join callback to report its native disconnect request', 5000)
    await waitUntil(() => child.hasFixtureOutput(
      'fixture JOIN_KICK_DISCONNECTED'),
    'rejected Fabric join to complete its disconnect callbacks', 5000)
    assert(!child.hasFixtureOutput('fixture JOIN_KICK_ENTITY_LOAD') &&
      !child.hasFixtureOutput('fixture JOIN_KICK_ENTITY_UNLOAD'),
    'rejected join does not emit an unmatched entity load or unload event')
    assert(serializedJoinKickReason.includes('fixture rejected join'),
    'Fabric join callback disconnect closes the admitted native session')
    const parallelCommandMarker =
      'fixture PARALLEL_COMMAND_NATIVE_RESULT true'
    const parallelCommandCount = fixtureMarkerCount(child, parallelCommandMarker)
    bot = await connectBot('JvmReenterA', gamePort)
    const pigSpawn = bot.entity.position.floored()
    const firstOverworldLoadPrefix = 'fixture ENTITY_LOAD_ID id='
    const firstOverworldLoadCount = fixtureEventCount(
      child, firstOverworldLoadPrefix)
    const pigSpawnResponse = await rcon.command(
      `summon minecraft:pig ${pigSpawn.x} ${pigSpawn.y} ${pigSpawn.z}`)
    try {
      await waitUntil(() => fixtureEventCount(
        child, firstOverworldLoadPrefix) > firstOverworldLoadCount,
      'Fabric entity-load callback runs after the mob is visible in its world',
      10000)
    } catch (error) {
      console.error(`overworld summon response: ${pigSpawnResponse}\n` +
        `JVM callback markers:\n${child.fixtureOutput}\n` +
        `server output:\n${child.output}`)
      throw error
    }
    const mobWrapperLoadMarker = 'fixture ENTITY_LOAD_MOB_WRAPPER true'
    await waitUntil(() => fixtureMarkerCount(child, mobWrapperLoadMarker) > 0,
      'Fabric entity-load listener to receive a MobEntity wrapper', 5000)
    assert(true,
      'live Fabric entity-load listener receives the canonical MobEntity wrapper')
    assert(child.hasFixtureOutput(
      'fixture ENTITY_CLASS type=minecraft:pig hostile=false animal=true fish=false maxHealth=10.0'),
    'native pigs expose their passive AnimalEntity class and actual maximum health')
    const firstOverworldLoad = lastFixtureEvent(
      child, firstOverworldLoadPrefix)
    const firstOverworldPigId = entityIdFromFixtureEvent(firstOverworldLoad)
    assert(firstOverworldLoad.includes('dimension=minecraft:overworld'),
      `first overworld pig load carries its exact entity id ${firstOverworldPigId}`)
    const wardenClassMarker =
      'fixture ENTITY_CLASS type=minecraft:warden hostile=true animal=false fish=false maxHealth=500.0'
    const wardenPos = new Vec3(pigSpawn.x + 8, pigSpawn.y, pigSpawn.z)
    await rcon.command(
      `summon minecraft:warden ${wardenPos.x} ${wardenPos.y} ${wardenPos.z}`)
    await waitUntil(() => child.hasFixtureOutput(wardenClassMarker),
      'native warden wrapper to expose hostile class and 500-health attribute',
      10000)
    assert(child.hasFixtureOutput(wardenClassMarker),
      'native wardens expose HostileEntity and their authoritative maximum health')
    await rcon.command('kill @e[type=minecraft:warden]')
    const netherLoadMarker =
      'fixture ENTITY_LOAD_PRESENT true dimension=minecraft:the_nether'
    const netherUnloadMarker =
      'fixture ENTITY_UNLOAD_PRESENT true dimension=minecraft:the_nether'
    const netherLoadCount = fixtureMarkerCount(child, netherLoadMarker)
    const netherUnloadCount = fixtureMarkerCount(child, netherUnloadMarker)
    const netherEntityLoadCount = fixtureEventCount(
      child, firstOverworldLoadPrefix)
    await rcon.command(
      'execute in minecraft:the_nether run summon minecraft:pig 0 64 0')
    await waitUntil(() => fixtureMarkerCount(child, netherLoadMarker) >
      netherLoadCount && fixtureEventCount(
        child, firstOverworldLoadPrefix) > netherEntityLoadCount,
    'Fabric entity-load callback receives the mob’s actual non-overworld world',
    10000)
    const netherLoadEvent = lastFixtureEvent(child, firstOverworldLoadPrefix)
    const netherPigId = entityIdFromFixtureEvent(netherLoadEvent)
    const netherUnloadPrefix =
      `fixture ENTITY_UNLOAD_ID id=${netherPigId} present=true dimension=minecraft:the_nether`
    await waitUntil(() => fixtureEventCount(child, netherUnloadPrefix) > 0 &&
      fixtureMarkerCount(child, netherUnloadMarker) > netherUnloadCount,
    'Fabric entity-unload callback to run on Nether no-player despawn', 10000)
    assert(true,
      `Nether unload is correlated to the same loaded entity id ${netherPigId}`)
    assert(true,
      'mob load callbacks run after insertion and carry the actual dimension')
    await rcon.command('gamerule doMobSpawning false')
    await rcon.command('kill @e[type=minecraft:pig]')
    await waitUntil(() => !Object.values(bot.entities).some(
      entity => entity.name === 'pig'),
    'existing pigs to be removed before the exact-handle removal fixture')
    const killProbePosition = new Vec3(
      pigSpawn.x + 2, pigSpawn.y, pigSpawn.z)
    await rcon.command(
      `summon minecraft:pig ${killProbePosition.x} ` +
      `${killProbePosition.y} ${killProbePosition.z}`)
    const killProbeEntity = await waitUntil(() =>
      Object.values(bot.entities).find(entity => entity.name === 'pig' &&
        entity.position.distanceTo(killProbePosition) < 1.25),
    'exact kill-probe pig to arrive in the Mineflayer entity roster', 10000)
    const killProbePigId = killProbeEntity.id
    assert(fixtureEventCount(child,
      `fixture ENTITY_LOAD_ID id=${killProbePigId} `) > 0,
    `kill-probe entity ${killProbePigId} has a Fabric load callback`)
    const killRequestMarker =
      `fixture ENTITY_KILL_REQUEST id=${killProbePigId} removed=true`
    const killUnloadMarker =
      `fixture ENTITY_KILL_UNLOAD_TARGET id=${killProbePigId} present=true`
    const killVerifyMarker =
      `fixture ENTITY_KILL_VERIFY id=${killProbePigId} absent=true`
    bot.chat(`cppfm_kill_probe:${killProbePigId}`)
    await waitUntil(() => fixtureMarkerCount(child, killRequestMarker) > 0,
      'Fabric Entity.remove(KILLED) targets the selected entity handle', 10000)
    await waitUntil(() => fixtureMarkerCount(child, killUnloadMarker) > 0,
      'KILLED removal dispatches unload for the same live entity handle',
      10000)
    await waitUntil(() => fixtureEventCount(child,
      `fixture ENTITY_UNLOAD_ID id=${killProbePigId} present=true dimension=minecraft:overworld`) > 0,
    'targeted overworld unload callback runs before roster removal', 10000)
    bot.chat('cppfm_kill_verify')
    await waitUntil(() => fixtureMarkerCount(child, killVerifyMarker) > 0,
      'KILLED removal invalidates that exact native handle and roster id',
      10000)
    assert(true,
      `overworld KILLED unload and handle invalidation match entity id ${killProbePigId}`)

    const discardLoadCount = fixtureMarkerCount(
      child, 'fixture ENTITY_LOAD_MOB_WRAPPER true')
    const discardUnloadTargetCount = fixtureMarkerCount(
      child, 'fixture ENTITY_DISCARD_UNLOAD_TARGET true')
    const discardUnloadPresentCount = fixtureMarkerCount(
      child, 'fixture ENTITY_DISCARD_UNLOAD_PRESENT true')
    const discardRequestCount = fixtureMarkerCount(
      child, 'fixture ENTITY_DISCARD_REQUEST true')
    const discardVerifyCount = fixtureMarkerCount(
      child, 'fixture ENTITY_DISCARD_VERIFY true')
    const discardProbePosition = new Vec3(
      pigSpawn.x + 2, pigSpawn.y, pigSpawn.z)
    await rcon.command(
      `summon minecraft:pig ${discardProbePosition.x} ` +
      `${discardProbePosition.y} ${discardProbePosition.z}`)
    const discardProbeEntity = await waitUntil(() =>
      Object.values(bot.entities).find(entity => entity.name === 'pig' &&
        entity.position.distanceTo(discardProbePosition) < 1.25),
    'exact discard-probe pig to arrive in the Mineflayer entity roster', 10000)
    const discardProbePigId = discardProbeEntity.id
    await waitUntil(() => fixtureMarkerCount(
      child, 'fixture ENTITY_LOAD_MOB_WRAPPER true') > discardLoadCount,
    'live discard-probe pig to load through the Fabric entity callback')
    bot.chat(`cppfm_discard_probe:${discardProbePigId}`)
    await waitUntil(() => fixtureMarkerCount(
      child, 'fixture ENTITY_DISCARD_REQUEST true') > discardRequestCount,
    'Java Entity.discard() to accept a native mob-removal request')
    await waitUntil(() => fixtureMarkerCount(
      child, 'fixture ENTITY_DISCARD_UNLOAD_TARGET true') >
        discardUnloadTargetCount,
    'discarded pig to dispatch its own Fabric unload before roster removal',
    10000)
    await waitUntil(() => fixtureMarkerCount(
      child, 'fixture ENTITY_DISCARD_UNLOAD_PRESENT true') >
        discardUnloadPresentCount,
    'discarded pig to remain visible in its world during Fabric unload', 5000)
    await sleep(250)
    // ENTITY_UNLOAD is intentionally dispatched before the authoritative
    // roster erase.  The client can observe that callback marker before the
    // current server tick has finished invalidating the handle, so retry the
    // read-only verification until the removal commit is visible.
    const discardVerifyDeadline = Date.now() + 10000
    while (fixtureMarkerCount(
      child, 'fixture ENTITY_DISCARD_VERIFY true') <= discardVerifyCount &&
      Date.now() < discardVerifyDeadline) {
      bot.chat('cppfm_discard_verify')
      await sleep(250)
    }
    await waitUntil(() => fixtureMarkerCount(
      child, 'fixture ENTITY_DISCARD_VERIFY true') > discardVerifyCount,
    'discarded Java mob handle and authoritative roster entry to be invalidated',
    10000)
    assert(true,
      'Java Entity.discard removes a live native mob without bypassing Fabric unload')

    bot.chat('/cppfm_callback_probe')
    try {
      await waitUntil(() => child.hasFixtureOutput(
        'fixture PLAYER_COMMAND_NATIVE_RESULT true'),
      'native server command re-entry from a real player-command callback',
      10000)
    } catch (error) {
      console.error(`JVM callback server output:\n${child.output}`)
      throw error
    }
    assert(true,
      'JVM command callback synchronously dispatches native work without holding the session simulation gate')

    concurrentBot = await connectBot('JvmReenterB', gamePort)
    bot.chat('cppfm_slow_chat_probe')
    await waitUntil(() => child.hasFixtureOutput(
      'fixture CHAT_CALLBACK_SLOW_ENTER'),
    'first real chat callback entering the controlled slow section', 10000)
    concurrentBot.chat('/cppfm_parallel_probe')
    await sleep(250)
    assert(fixtureMarkerCount(child, parallelCommandMarker) ===
        parallelCommandCount,
    'a second-client callback waits in order instead of running concurrently with mod code')
    await waitUntil(() => child.hasFixtureOutput(
      'fixture CHAT_CALLBACK_SLOW_EXIT'),
    'first real chat callback completing its controlled slow section', 10000)
    await waitUntil(() => fixtureMarkerCount(child, parallelCommandMarker) >
      parallelCommandCount,
    'queued second-client Fabric command to run after the slow callback', 10000)
    const callbackOrder = fixtureSequence(child, [
      'fixture CHAT_CALLBACK_SLOW_ENTER',
      'fixture CHAT_CALLBACK_SLOW_EXIT',
      'fixture PARALLEL_COMMAND_NATIVE_RESULT true'
    ])
    if (!callbackOrder) {
      const callbackLines = ['stderr', 'stdout'].map(streamName =>
        `${streamName}:\n${child.fixtureEventsByStream[streamName].filter(line =>
          line.includes('fixture CHAT_CALLBACK_SLOW_') ||
          line.includes('fixture PARALLEL_COMMAND_NATIVE_RESULT')).join('\n')}`)
      console.error(`JVM callback order was not present on either single stream\n` +
        callbackLines.join('\n'))
    }
    assert(callbackOrder !== null,
    'serialized Fabric callbacks preserve order and complete native re-entry after a slow callback')

    const combatFeet = bot.entity.position.floored()
    const combatY = Math.floor(bot.entity.position.y)
    await rcon.command(
      `setblock ${combatFeet.x} ${combatY - 1} ${combatFeet.z} minecraft:stone`)
    await rcon.command(
      `setblock ${combatFeet.x + 1} ${combatY - 1} ${combatFeet.z} minecraft:stone`)
    await rcon.command(`gamemode survival ${bot.username}`)
    await rcon.command(`gamemode survival ${concurrentBot.username}`)
    await rcon.command(
      `tp ${bot.username} ${combatFeet.x + 0.5} ${combatY} ${combatFeet.z + 0.5}`)
    await rcon.command(
      `tp ${concurrentBot.username} ${combatFeet.x + 1.5} ${combatY} ${combatFeet.z + 0.5}`)
    const victimEntity = await waitUntil(
      () => bot.players[concurrentBot.username]?.entity || null,
      'first Mineflayer client to track the PvP-cancellation victim')
    await waitUntil(() => {
      const attackerPosition = bot.entity.position
      const victimPosition = victimEntity.position
      return Math.abs(attackerPosition.x - victimPosition.x) <= 2 &&
        Math.abs(attackerPosition.z - victimPosition.z) <= 2
    }, 'Mineflayer combat clients to reach the adjacent PvP fixture')
    await sleep(300)
    const victimEntityId = victimEntity.id
    const canceledDamageMarker = 'fixture DAMAGE_CANCEL_VICTIM'
    const canceledDamageCount = fixtureEventCount(child, canceledDamageMarker)
    const canceledDamageAfterAppliedMarker =
      'fixture DAMAGE_CANCEL_AFTER_APPLIED'
    const canceledDamageAfterAppliedCount = fixtureEventCount(
      child, canceledDamageAfterAppliedMarker)
    const canceledNativeHealthMarker =
      'fixture DAMAGE_CANCEL_NATIVE_HEALTH id='
    const canceledNativeHealthCount = fixtureEventCount(
      child, canceledNativeHealthMarker)
    let victimVelocityPackets = 0
    const onVictimVelocity = packet => {
      if (packet.entityId === victimEntityId) victimVelocityPackets++
    }
    concurrentBot._client.on('entity_velocity', onVictimVelocity)
    const victimHealthBefore = concurrentBot.health
    try {
      bot.attack(victimEntity)
      await waitUntil(() => fixtureEventCount(child, canceledDamageMarker) >
        canceledDamageCount,
      'real PvP hit to reach the Fabric AllowDamage cancellation callback',
      10000)
      await waitUntil(() => fixtureEventCount(
        child, canceledNativeHealthMarker) > canceledNativeHealthCount,
      'canceled PvP hit to report authoritative native health after dispatch',
      10000)
      const nativeHealthEvent = lastFixtureEvent(
        child, canceledNativeHealthMarker)
      const nativeHealthMatch = /id=(\d+) before=([-+\d.eE]+) after=([-+\d.eE]+)/
        .exec(nativeHealthEvent)
      assert(nativeHealthMatch !== null &&
          Number(nativeHealthMatch[1]) === victimEntityId &&
          Number(nativeHealthMatch[2]) === Number(nativeHealthMatch[3]),
      'Fabric-canceled damage leaves authoritative native health unchanged')
      await sleep(300)
      assert(concurrentBot.health === victimHealthBefore,
        'Fabric-cancelled PvP hit leaves the client health unchanged')
      assert(fixtureEventCount(child, canceledDamageAfterAppliedMarker) ===
          canceledDamageAfterAppliedCount,
        'Fabric-cancelled PvP hit does not dispatch AFTER_DAMAGE')
      assert(victimVelocityPackets === 0,
        'Fabric-cancelled PvP hit sends no knockback velocity to the victim')
    } finally {
      concurrentBot._client.removeListener(
        'entity_velocity', onVictimVelocity)
    }

    const feet = bot.entity.position.floored()
    const supportPos = new Vec3(feet.x + 2, feet.y - 1, feet.z)
    const placedPos = new Vec3(supportPos.x, supportPos.y + 1, supportPos.z)
    await rcon.command(
      `setblock ${supportPos.x} ${supportPos.y} ${supportPos.z} minecraft:stone`)
    await rcon.command(
      `setblock ${placedPos.x} ${placedPos.y} ${placedPos.z} minecraft:air`)
    await rcon.command(`give ${bot.username} minecraft:cobblestone 1`)
    await waitUntil(() => countItems(bot.inventory.items(), 'cobblestone') === 1,
      'JVM callback placement item to reach the real client')
    const cobblestone = bot.inventory.items().find(
      item => item.name === 'cobblestone')
    await bot.equip(cobblestone, 'hand')
    const support = await waitUntil(() => {
      const block = bot.blockAt(supportPos)
      return block?.name === 'stone' ? block : null
    }, 'JVM callback placement support to reach the real client')
    await withTimeout(bot.placeBlock(support, new Vec3(0, 1, 0)),
      15000, 'real-client placement through JVM callbacks')
    await waitUntil(() => child.hasFixtureOutput(
      'fixture BLOCK_CLICK_NATIVE_RESULT true'),
    'NativeBridge command completion from a real block-click callback', 10000)
    await waitUntil(() => child.hasFixtureOutput(
      'fixture BLOCK_PLACE_NATIVE_RESULT true'),
    'NativeBridge command completion from a real block-place callback', 10000)
    assert(true,
      'real Mineflayer placement completes re-entrant native work from click and place callbacks')

    await rcon.command(`gamemode creative ${bot.username}`)
    bot._client.write('block_dig', {
      status: 0, location: placedPos, face: 1, sequence: 0
    })
    await waitUntil(() => child.hasFixtureOutput(
      'fixture BLOCK_BREAK_NATIVE_RESULT true'),
    'NativeBridge command completion from a real creative block-break callback',
    10000)
    await waitForRconBlockState(rcon, placedPos, 'minecraft:air',
      'JVM callback block break to finish on the authoritative server')
    assert(true,
      'real Mineflayer block break completes re-entrant native work from the break callback')
    assert(!child.callbackThreadMismatch,
      'Fabric event callbacks stay serialized on the authoritative server thread')
  } catch (error) {
    primaryError = error
    if (child) {
      console.error(`JVM callback probe server output:\n${child.output}`)
      console.error(`JVM callback probe fixture markers:\n${child.fixtureOutput}`)
    }
    throw error
  } finally {
    await stopBot(concurrentBot)
    await stopBot(bot)
    if (rcon) rcon.close()
    let stopError = null
    try { await stopOwnedServer(child, true) }
    catch (error) { stopError = error }
    const serverExited = !child || child.pid == null ||
      child.exitCode !== null || child.signalCode !== null
    if (serverExited && !stopError) {
      try { await fs.rm(root, { recursive: true, force: true }) }
      catch (error) { stopError ||= error }
    } else {
      console.error(`retaining JVM callback fixture world after incomplete cleanup: ${root}`)
    }
    if (stopError) {
      if (primaryError)
        console.error(`JVM fixture cleanup also failed: ${stopError.stack || stopError}`)
      else
        throw stopError
    }
  }
}

function startManagedSmoke(operation, durationMs, description) {
  const watchdog = setTimeout(() => {
    controller.abort(new Error(
      `${description} exceeded its ${Math.round(durationMs / 1000)}-second deadline`))
  }, durationMs)
  for (const name of ['SIGINT', 'SIGTERM']) {
    const handler = () => {
      receivedSignal = name
      controller.abort(new Error(`received ${name}; cleaning up owned test processes`))
    }
    signalHandlers.set(name, handler)
    process.once(name, handler)
  }

  return Promise.resolve().then(operation).catch(error => {
    console.error(error.stack || error)
    process.exitCode = receivedSignal === 'SIGINT' ? 130 :
                       receivedSignal === 'SIGTERM' ? 143 : 1
  }).finally(() => {
    clearTimeout(watchdog)
    for (const [name, handler] of signalHandlers)
      process.removeListener(name, handler)
  })
}

function startLiveSmoke() {
  return startManagedSmoke(main, 540000,
                           'Mineflayer gameplay and JVM callback scenario')
}

function startJvmCallbackSmoke() {
  return startManagedSmoke(() => runJvmCallbackProbe(binaryPath), 90000,
                           'Mineflayer JVM callback scenario')
}

module.exports = {
  RconClient,
  startLiveSmoke,
  startJvmCallbackSmoke
}

if (require.main === module) startLiveSmoke()
