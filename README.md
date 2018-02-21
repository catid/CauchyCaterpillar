# CauchyCaterpillar
CCat : The Cauchy Caterpillar - Streaming Erasure Code With Short Memory

ccat.h provides a simple C API for streaming erasure codes with a
fixed window.  This is designed to be used by low-latency netcode.
It generates redundant packets that can fill in for lost originals
in a stream of data probably sent over UDP sockets.

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

Applications using the library can use different locks to protect the
ccat_encode_xxx() functions and the ccat_decode_xxx() functions, because no data
is shared between those.  Otherwise the library is not thread-safe and
does require locking on the application-side.

#### Packet de-duplication:

CCat will not deliver two packets with the same sequence number.

#### Packet re-ordering:

CCat can deliver data out of order, so its output should be fed into
a dejitter buffer.

#### Limitations and alternatives:

It supports up to 30% redundancy, and so loss rates above about 20%
are too high for it to effectively handle.

TODO: Determine how much FEC to send at different data rates/loss rates.

For faster streams, using Siamese FEC is recommended:
https://github.com/catid/siamese/

For fountain codes, using Wirehair FEC is recommended:
https://github.com/catid/wirehair/

Compared to Random Linear Codes, the decoding 2+ losses is 2x more likely
(0.2% measured failure rate versus 0.4% for RLC).

Encoding/decoding is a bit faster than RLC thanks to the Cauchy matrix structure.

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

#### Credits

I posted about this type of erasure code years ago on my blog, but never finished the project.  Recently Nicolas SAID sent me a paper from Dr. Martin Reisslein from ASU that explores a similar idea ( http://mre.faculty.asu.edu/CRLNC.pdf ).  Based on the success of that work I decided to put my own spin on it and release here.  Hope others find it useful!

Software by Christopher A. Taylor <mrcatid@gmail.com>

Please reach out if you need support or would like to collaborate on a project.
