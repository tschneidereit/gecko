// |reftest| skip-if(!this.hasOwnProperty("ReadableStream"))

otherGlobal = newGlobal();

ReadableStreamReader = new ReadableStream().getReader().constructor;
OtherReadableStreamReader = new otherGlobal.ReadableStream().getReader().constructor;
OtherReadableStream = otherGlobal.ReadableStream;

stream = new ReadableStream({
    start(c) {
        controller = c;
    }
});
otherReader = OtherReadableStream.prototype.getReader.call(stream);
assertEq(otherReader instanceof ReadableStreamReader, false);
assertEq(otherReader instanceof OtherReadableStreamReader, true);

request = otherReader.read();

controller.error({});

otherReader.releaseLock();

reader = stream.getReader();
reader.read();

if (typeof reportCompare === "function")
    reportCompare(true, true);
