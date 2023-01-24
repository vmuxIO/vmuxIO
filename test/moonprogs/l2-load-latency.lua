local mg     = require "moongen"
local memory = require "memory"
local device = require "device"
local ts     = require "timestamping"
local stats  = require "stats"
local hist   = require "histogram"
local timer  = require "timer"

local function getRstFile(...)
	local args = { ... }
	for i, v in ipairs(args) do
		result, count = string.gsub(v, "%-%-result%=", "")
		if (count == 1) then
			return i, result
		end
	end
	return nil, nil
end

function configure(parser)
	parser:description("Generates bidirectional CBR traffic with hardware rate control and measure latencies.")
	parser:argument("dev", "Device to transmit/receive from."):convert(tonumber)
	parser:argument("mac", "MAC address of the destination device.")
	parser:option("-r --rate", "Transmit rate in kpps."):default(14000):convert(tonumber)
    parser:option("-s --size", "Packet size in bytes."):default(60):convert(tonumber)
	parser:option("-f --file", "Filename of the latency histogram."):default("histogram.csv")
	parser:option("-t --time", "Time to transmit for in seconds."):default(60):convert(tonumber)
end

function master(args)
	local dev = device.config({port = args.dev, rxQueues = 2, txQueues = 2})
	device.waitForLinks()
	dev:getTxQueue(0):setRate(args.rate * (args.size + 4) * 8 / 1000)
	mg.startTask("loadSlave", dev:getTxQueue(0), dev:getMac(true), args.mac, args.size)
	stats.startStatsTask{dev}
	mg.startSharedTask("timerSlave", dev:getTxQueue(1), dev:getRxQueue(1), args.mac, args.file)
	if args.time >= 0 then
		runtime = timer:new(args.time)
		runtime:wait()
		mg:stop()
	end
	mg.waitForTasks()
end

function loadSlave(queue, srcMac, dstMac, pktSize)
	local mem = memory.createMemPool(function(buf)
		buf:getEthernetPacket():fill{
			ethSrc = srcMac,
			ethDst = dstMac,
			ethType = 0x1234
		}
	end)
	local bufs = mem:bufArray()
	while mg.running() do
		bufs:alloc(pktSize)
		queue:send(bufs)
	end
end

function timerSlave(txQueue, rxQueue, dstMac, histfile)
	local timestamper = ts:newTimestamper(txQueue, rxQueue)
	local hist = hist:new()
	mg.sleepMillis(1000) -- ensure that the load task is running
	while mg.running() do
		hist:update(timestamper:measureLatency(function(buf) buf:getEthernetPacket().eth.dst:setString(dstMac) end))
	end
	hist:print()
	hist:save(histfile)
end
