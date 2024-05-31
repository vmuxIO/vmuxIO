local mg     = require "moongen"
local memory = require "memory"
local device = require "device"
local ts     = require "timestamping"
local stats  = require "stats"
local hist   = require "histogram"
local timer  = require "timer"
local log    = require "log"

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
	parser:option("-c --csv", "Filename of the output csv."):default("")
	parser:option("-t --threads", "Number of threads per device."):args(1):convert(tonumber):default(1)
	parser:option("-T --time", "Time to transmit for in seconds."):default(60):convert(tonumber)
	parser:option("-m --macs", "Send in round robin to (ethDst...ethDst+macs)."):default(0):convert(tonumber)
end

function master(args)
	local dev = device.config({port = args.dev, rxQueues = args.threads + 1, txQueues = args.threads + 1})
	device.waitForLinks()
	for i = 0, args.threads - 1 do
	    dev:getTxQueue(i):setRate(args.rate * (args.size + 4) * 8 / 1000)
	    mg.startTask("loadSlave", dev:getTxQueue(i), dev:getMac(true), args.mac, args.size, args.macs)
    end
	stats.startStatsTask{dev}
	if args.csv ~= "" then
		stats.startStatsTask{devices={dev}, format="csv", file=args.csv}
	end
	mg.startSharedTask("timerSlave", dev:getTxQueue(args.threads), dev:getRxQueue(args.threads), args.mac, args.file)
	if args.time >= 0 then
		runtime = timer:new(args.time)
		runtime:wait()
		mg:stop()
	end
	mg.waitForTasks()
end

function setMac(buf, mac_nr)
  local pl = buf:getRawPacket().payload
  pl.uint8[5] = bit.band(mac_nr, 0xFF)
  pl.uint8[4] = bit.band(bit.rshift(mac_nr, 8), 0xFF)
  pl.uint8[3] = bit.band(bit.rshift(mac_nr, 16), 0xFF)
  pl.uint8[2] = bit.band(bit.rshift(mac_nr, 24), 0xFF)
  pl.uint8[1] = bit.band(bit.rshift(mac_nr + 0ULL, 32ULL), 0xFF)
  pl.uint8[0] = bit.band(bit.rshift(mac_nr + 0ULL, 40ULL), 0xFF)
end

function sendSimple(queue, bufs, pktSize)
	while mg.running() do
    bufs:alloc(pktSize)
    queue:send(bufs)
  end
end

function sendMacs(queue, bufs, pktSize, dstMac, numDstMacs)
	if numDstMacs > 0xFF then
		error("Sending packets with this many mac addresses is unsupported!")
	end
	local mac_nr = parseMacAddress(dstMac, 1)
	while mg.running() do
    bufs:alloc(pktSize)
    for i, buf in ipairs(bufs) do
      local dst = mac_nr + bit.lshift(((i-1) % numDstMacs) + 0ULL, 40ULL)
      -- print(dst)
      setMac(buf, dst)
    end
    queue:send(bufs)
  end
end

function loadSlave(queue, srcMac, dstMac, pktSize, numDstMacs)
	local mem = memory.createMemPool(function(buf)
		buf:getEthernetPacket():fill{
			ethSrc = srcMac,
			ethDst = dstMac,
			ethType = 0x1234
		}
	end)
	local bufs = mem:bufArray()
	if numDstMacs == 0 then
		sendSimple(queue, bufs, pktSize)
	else
		sendMacs(queue, bufs, pktSize, dstMac, numDstMacs)
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
