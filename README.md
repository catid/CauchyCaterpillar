# CauchyCaterpillar
CCat : The Cauchy Caterpillar - Streaming Erasure Code With Short Memory

ccat.h provides a simple C API for streaming erasure codes with a
fixed window.  This is designed to be used by low-latency netcode.
It generates redundant packets that can fill in for lost originals
in a stream of UDP datagrams.

Example applications:
* VoIP
* Video conferencing/live broadcast
* Live telemetry/statistics feeds

#### Usage:

(1) Call ccat_create() to create a CCatCodec object.

(2) When sending a packet, pass it to ccat_encode_original().

(3) To encode recovery data, call ccat_encode_recovery(), which generates
a packet that can be sent over the network to fill in for losses.

(4) When receiving a packet, pass originals to ccat_decode_original().
Pass encoded data to the ccat_decode_recovery() function.  When recovery
occurs it will call the application's OnRecoveredData() callback.

There is a simple unit test here, which also demonstrates the C++ SDK wrapper:
https://github.com/catid/CauchyCaterpillar/blob/master/tests/Tester.cpp

#### Thread-safety:

Applications can use different locks to protect the ccat_encode_xxx() functions
and the ccat_decode_xxx() functions, because no data is shared between those.
Otherwise the library is not thread-safe.

#### Packet de-duplication:

CCat will not deliver two packets with the same sequence number.

#### Packet re-ordering:

CCat can deliver data out of order.

#### How many FEC packets should be sent?

Based on my simulations you should send at minimum 1.75x the packetloss rate (PLR) or it will not be effective.  I'd recommend just doubling the detected PLR.

![alt text](https://github.com/catid/CauchyCaterpillar/raw/master/docs/gack_top_plr_fec.png "Operation hull for PLR versus FEC")

The tester generates the minimum "effective packetloss" from a set of 1000 parallel simulations.  The operation hull above is the region in which Cauchy Caterpillar has a minimum effective packetloss of 1% or better.  The script that generated it is in the docs: [gack_generator_js.txt](https://github.com/catid/CauchyCaterpillar/raw/master/docs/gack_generator_js.txt).

What this demonstrates is there's a roughly linear relationship between minimum FEC rate and PLR, with a slope of about 1.75.

#### Limitations and alternatives:

It supports up to 30% redundancy, and so loss rates above about 20% are too high for it to handle.

For a window of 100 milliseconds: The maximum original data stream rate is 2500 packets/second for 1% PLR down to 2000 packets/second for 10% PLR.  This limits the codec to about 2000 packets/second * 1400 bytes/packet = 2.8 Megabytes/second or less.

![alt text](https://github.com/catid/CauchyCaterpillar/raw/master/docs/gack_side_data_rate.png "Operation hull for Data Rate")

From the operation hull it's clear there's some non-linear relationship between FEC/PLR/DataRate, but basically it ranges from 2K to 2.5K packets/second before the code starts failing.

For faster streams, using Siamese FEC is recommended:
https://github.com/catid/siamese/

For fountain codes, using Wirehair FEC is recommended:
https://github.com/catid/wirehair/

Compared to Random Linear Codes, the decoding 2+ losses is 2x more likely
(0.2% measured failure rate versus 0.4% for RLC).

Encoding/decoding is a bit faster than RLC thanks to the Cauchy matrix structure.

#### Future work:

The limit on data rate (< 2000 packets/second) and the CPU overhead required can be alleviated by using a more complicated matrix structure like the one I developed for Siamese.

#### Streaming versus Generational Block Codec

There is an older generational block codec that I wrote a while back that you can find here:
https://github.com/catid/shorthair

The advantages of streaming erasure codes over generational block codes are covered here:

[Block or Convolutional AL-FEC Codes? A Performance
Comparison for Robust Low-Latency Communications](https://hal.inria.fr/hal-01395937v2/document) [1].

~~~
[1] Vincent Roca, Belkacem Teibi, Christophe Burdinat, Tuan Tran-Thai, C´edric Thienot. Block
or Convolutional AL-FEC Codes? A Performance Comparison for Robust Low-Latency Communications.
2017. <hal-01395937v2>
~~~

CCat corresponds to the "brief glimpse at the future" slide here:
https://github.com/catid/CauchyCaterpillar/blob/master/docs/ErasureCodesInSoftware.pdf

#### Packet Format

I left the packet format up to the application, since it's application-specific.  Here's some guidance about how to compress the fields:

(1) If the application has a back-channel (e.g. acknowledging receipt of some data), then it's possible to compress the overhead a bit. You can truncate each field to a number of low bytes, and then re-expand using this algorithm:

https://github.com/catid/CauchyCaterpillar/blob/master/Counter.h#L333

The number of bytes to truncate to can be calculated with this bit of tested code:

    // This is provided in ACK data from the peer's back-channel.
    int32_t PeerNextExpectedSeq = 0;
    
    // This is the next incrementing sequence number we will generate.
    int32_t NextSequenceNumber = 0;

    /// Get number of bytes used to represent the given packet number in our protocol
    /// taking into account the peer's next expected sequence number
    unsigned getPacketNumBytes(int32_t packetNum) const
    {
        // The peer uses its next expected sequence number to decompress the
        // numbers we send.  By the time a new datagram arrives, that number
        // may have advanced from that last one acknowledged through the last
        // one that was sent.  When sending a new sequence number, include
        // enough bits so that any possible number can decode the new one.
        // So, take the larger of the distances from each end.
        int32_t mag = packetNum - PeerNextExpectedSeq;
        if (mag < 0)
            mag = 1 - mag;
        int32_t mag2 = packetNum - NextSequenceNumber;
        if (mag2 < 0)
            mag2 = 1 - mag2;
        if (mag < mag2)
            mag = mag2;

        // Choose the number of bytes to send:
        if (mag < 0x80)
            return 1;
        else if (mag < 0x8000)
            return 2;
        // Add other cases here for 3+ bytes.  I stopped at 3 bytes for my protocol.
        else
            return 3;
    }

In my protocol I use 2 bits in a header byte to represent how many bytes are used by the sequence number field, so it could represent some range like {0,1,2,3} -> {1,2,3,7} or {1,2,3,4} bytes, etc.  It depends a lot on the application.

So it's a bit complicated, but you can use these two functions to compress 64-bit numbers down to 1 byte for slow traffic, and it won't cause any problems.

(2) If there is no back-channel then you can compress the fields to get rid of leading zeros. So an encoding where the high/low bit means "more bytes are needed" and you stream in a byte or two at a time. Example of that is here:

https://github.com/catid/siamese/blob/master/SiameseSerializers.h#L566

(3) Otherwise you can still pick some reasonable upper-limit and make some assumptions and truncate to like 2-4 bytes.

uint64_t SequenceStart - Compressed as above

Data - No compression

Bytes - Can be implied by the frame it is in. Otherwise it won't exceed 2 bytes

Count - 1 byte - Does not exceed 192. You can use higher values like 255 to indicate an escape code to a different type of packet..

RecoveryRow - 1 byte (6 bits) - You can use the remaining 2 bits for something else like signaling it's an original versus recovery packet.

#### Credits

I posted about this type of erasure code years ago on my blog, but never finished the project.  Recently Nicolas SAID sent me a paper from Dr. Martin Reisslein that explores a similar idea ( http://mre.faculty.asu.edu/CRLNC.pdf ).  Based on the success of that work I decided to put my own spin on it and release here.  I hope others find it useful!

Software by Christopher A. Taylor <mrcatid@gmail.com>

Please reach out if you need support or would like to collaborate on a project.
