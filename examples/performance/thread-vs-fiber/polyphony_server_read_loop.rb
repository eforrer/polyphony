# frozen_string_literal: true

require 'bundler/setup'
require 'polyphony'
require 'http/parser'

$connection_count = 0

def handle_client(socket)
  $connection_count += 1
  parser = Http::Parser.new
  reqs = []
  parser.on_message_complete = proc do |env|
    reqs << Object.new # parser
  end
  socket.read_loop do |data|
    parser << data
    while (req = reqs.shift)
      handle_request(socket, req)
    end
  end
rescue IOError, SystemCallError => e
  # do nothing
ensure
  $connection_count -= 1
  socket&.close
end

def handle_request(client, parser)
  status_code = "200 OK"
  data = "Hello world!\n"
  headers = "Content-Type: text/plain\r\nContent-Length: #{data.bytesize}\r\n"
  client.write "HTTP/1.1 #{status_code}\r\n#{headers}\r\n#{data}"
end

spin do
  server = TCPServer.open('0.0.0.0', 1234)
  puts "listening on port 1234"

  Thread.current.agent.accept_loop(server) do |client|
    spin { handle_client(client) }
  end
  # loop do
  #   client = server.accept
  #   spin { handle_client(client) }
  # end
ensure
  server&.close
end

# every(1) {
#   stats = Thread.current.fiber_scheduling_stats
#   stats[:connection_count] = $connection_count
#   puts "#{Time.now} #{stats}"
# }

puts "pid #{Process.pid}"
suspend