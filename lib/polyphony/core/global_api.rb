# frozen_string_literal: true

require_relative '../extensions/core'
require_relative '../extensions/fiber'
require_relative './exceptions'
require_relative './throttler'

module Polyphony
  # Global API methods to be included in ::Object
  module GlobalAPI
    def after(interval, &block)
      spin do
        sleep interval
        block.()
      end
    end

    def cancel_after(interval, with_exception: Polyphony::Cancel, &block)
      fiber = ::Fiber.current
      canceller = spin do
        sleep interval
        exception = with_exception.is_a?(Class) ?
          with_exception.new : RuntimeError.new(with_exception)
        fiber.schedule exception
      end
      block ? cancel_after_wrap_block(canceller, &block) : canceller
    end

    def cancel_after_wrap_block(canceller, &block)
      block.call
    ensure
      canceller.stop
    end

    def spin(tag = nil, &block)
      Fiber.current.spin(tag, caller, &block)
    end

    def spin_loop(tag = nil, rate: nil, &block)
      if rate
        Fiber.current.spin(tag, caller) do
          throttled_loop(rate, &block)
        end
      else
        Fiber.current.spin(tag, caller) { loop(&block) }
      end
    end

    def every(interval)
      next_time = ::Process.clock_gettime(::Process::CLOCK_MONOTONIC) + interval
      loop do
        now = ::Process.clock_gettime(::Process::CLOCK_MONOTONIC)
        Thread.current.agent.sleep(next_time - now)
        yield
        loop do
          next_time += interval
          break if next_time > now
        end
      end
    end

    def move_on_after(interval, with_value: nil, &block)
      fiber = ::Fiber.current
      unless block
        return spin do
          sleep interval
          fiber.schedule with_value
        end
      end

      move_on_after_with_block(fiber, interval, with_value, &block)
    end

    def move_on_after_with_block(fiber, interval, with_value, &block)
      canceller = spin do
        sleep interval
        fiber.schedule Polyphony::MoveOn.new(with_value)
      end
      block.call
    rescue Polyphony::MoveOn => e
      e.value
    ensure
      canceller.stop
    end

    def receive
      Fiber.current.receive
    end

    def receive_pending
      Fiber.current.receive_pending
    end

    def supervise(*args, &block)
      Fiber.current.supervise(*args, &block)
    end

    def sleep(duration = nil)
      return sleep_forever unless duration

      Thread.current.agent.sleep duration
    end

    def sleep_forever
      Thread.current.agent.ref
      loop { sleep 60 }
    ensure
      Thread.current.agent.unref
    end

    def throttled_loop(rate, count: nil, &block)
      throttler = Polyphony::Throttler.new(rate)
      if count
        count.times { |_i| throttler.(&block) }
      else
        loop { throttler.(&block) }
      end
    ensure
      throttler&.stop
    end
  end
end

Object.include Polyphony::GlobalAPI
