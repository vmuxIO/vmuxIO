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
	parser:option("-t --threads", "Number of threads per device."):args(1):convert(tonumber):default(1)
	parser:option("-T --time", "Time to transmit for in seconds."):default(60):convert(tonumber)
end

function master(args)
	local dev = device.config({port = args.dev, rxQueues = args.threads + 1, txQueues = args.threads + 1})
	device.waitForLinks()
	for i = 0, args.threads - 1 do
	    -- dev:getTxQueue(i):setRate(args.rate * (args.size + 4) * 8 / 1000)
	    mg.startTask("loadSlave", dev, dev:getTxQueue(i), dev:getMac(true), args.mac, args.size, args.rate)
    end
	-- stats.startStatsTask{dev}
	mg.startSharedTask("timerSlave", dev:getTxQueue(args.threads), dev:getRxQueue(args.threads), args.mac, args.file)
	-- if args.time >= 0 then
	-- 	runtime = timer:new(args.time)
	-- 	runtime:wait()
	-- 	mg:stop()
	-- end
	mg.waitForTasks()
end

function loadSlave(dev, queue, srcMac, dstMac, pktSize, rate)
	local mem = memory.createMemPool(function(buf)
		buf:getEthernetPacket():fill{
			ethSrc = srcMac,
			ethDst = dstMac,
			ethType = 0x1234
		}
	end)
	local bufs = mem:bufArray()
	local rxStats = stats:newDevRxCounter(dev, "plain")
	local txStats = stats:newManualTxCounter(dev, "plain")
	while mg.running() do
		bufs:alloc(pktSize)
		for _, buf in ipairs(bufs) do
			-- this script uses Mpps instead of Mbit (like the other scripts)
			-- 1000 is a randomly chosen value that seems to have the desired effect
			buf:setDelay(poissonDelay(1000 * 10^10 / 8 / (rate * 10^6) - pktSize - 24))
			--buf:setRate(rate)
		end
		txStats:updateWithSize(queue:sendWithDelay(bufs), pktSize)
		rxStats:update()
		--txStats:update()
	end
	rxStats:finalize()
	txStats:finalize()
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
