'use strict'

const assert = require('node:assert/strict')
const { EventEmitter } = require('node:events')
const { test } = require('node:test')
const { observeServerOutput } = require('./server_output.cjs')

test('stdout and stderr fragments are never combined into fabricated evidence', () => {
  const child = {
    stdout: new EventEmitter(),
    stderr: new EventEmitter()
  }
  observeServerOutput(child)

  child.stdout.emit('data', Buffer.from('fixture CALLBACK_THREAD_'))
  child.stderr.emit('data', Buffer.from('MISMATCH\n'))
  assert.equal(child.callbackThreadMismatch, false)
  assert.equal(child.hasServerOutput('fixture CALLBACK_THREAD_MISMATCH'), false)
  assert.equal(child.hasFixtureOutput('fixture CALLBACK_THREAD_MISMATCH'), false)
  assert.equal(child.fixtureEventsByStream.stdout.length, 0)
  assert.equal(child.fixtureEventsByStream.stderr.length, 0)
})

test('same-stream callback-thread evidence stays latched after output truncation', () => {
  const child = {
    stdout: new EventEmitter(),
    stderr: new EventEmitter()
  }
  observeServerOutput(child)

  child.stdout.emit('data', Buffer.from('fixture CALLBACK_THREAD_'))
  child.stdout.emit('data', Buffer.from('MISMATCH\n'))
  child.stdout.emit('data', Buffer.from('routine server log '.repeat(2000)))

  assert.equal(child.outputByStream.stdout.length, 24000)
  assert.equal(child.output.includes('fixture CALLBACK_THREAD_MISMATCH'), false)
  assert.equal(child.output.includes('fixture CALLBACK_THREAD_MISMATCH'), false)
  assert.equal(child.callbackThreadMismatch, true)
  assert.equal(child.hasFixtureOutput('fixture CALLBACK_THREAD_MISMATCH'), true)
})
