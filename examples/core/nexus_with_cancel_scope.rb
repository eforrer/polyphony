# frozen_string_literal: true

require 'modulation'

Nuclear = import('../../lib/nuclear')

async def my_sleep(t)
  puts "start: #{t}"
  r = await sleep(t)
  puts "my_sleep result #{r.inspect}"
  puts "done: #{t}"
end

spawn do
  puts "#{Time.now} going to sleep..."
  move_on_after(0.5) do
    await Nuclear.nexus do |f|
      f << my_sleep(1)
      f << my_sleep(2)
      f << my_sleep(3)
    end
    puts "nexus done"
  end
  puts "#{Time.now} woke up"
end
