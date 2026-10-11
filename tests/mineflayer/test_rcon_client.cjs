'use strict'

const assert = require('node:assert/strict')
const { EventEmitter } = require('node:events')
const path = require('node:path')
const Module = require('node:module')
const { test } = require('node:test')
process.env.NODE_PATH = [
  path.join(__dirname, 'node_modules'),
  process.env.NODE_PATH
].filter(Boolean).join(path.delimiter)
Module._initPaths()
const { RconClient } = require('../mineflayer_live_smoke.cjs')

function encodePacket(id, type, body) {
  const text = Buffer.from(body, 'utf8')
  const frame = Buffer.alloc(4 + 8 + text.length + 2)
  frame.writeInt32LE(10 + text.length, 0)
  frame.writeInt32LE(id, 4)
  frame.writeInt32LE(type, 8)
  text.copy(frame, 12)
  return frame
}

class FakeRconSocket extends EventEmitter {
  write(frame) {
    const id = frame.readInt32LE(4)
    const text = frame.subarray(12, frame.length - 2).toString('utf8')
    const delay = text === 'late' ? 40 : text === 'slow' ? 20 : 1
    setTimeout(() => {
      this.emit('data', encodePacket(id, 2, `response:${text}`))
    }, delay)
    return true
  }

  end() {}
  destroy() {}
}

test('timed-out and out-of-order RCON replies cannot satisfy another request', async () => {
  const client = new RconClient(new FakeRconSocket())
  client.responseTimeoutMs = 10

  await assert.rejects(client.command('late'), /timed out waiting for RCON response/)
  await new Promise(resolve => setTimeout(resolve, 45))
  assert.equal(await client.command('next'), 'response:next')
  assert.equal(client.waiters.size, 0)

  client.responseTimeoutMs = 100
  const slow = client.command('slow')
  const fast = client.command('fast')
  assert.deepEqual(await Promise.all([slow, fast]), [
    'response:slow',
    'response:fast'
  ])
  assert.equal(client.waiters.size, 0)
})
