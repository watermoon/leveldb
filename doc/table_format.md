leveldb File format
===================

    <beginning_of_file>
    [data block 1]
    [data block 2]
    ...
    [data block N]
    [meta block 1]
    ...
    [meta block K]
    [metaindex block]
    [index block]
    [Footer]        (fixed size; starts at file_size - sizeof(Footer))
    <end_of_file>

The file contains internal pointers.  Each such pointer is called
a BlockHandle and contains the following information:

    offset:   varint64
    size:     varint64

See [varints](https://developers.google.com/protocol-buffers/docs/encoding#varints)
for an explanation of varint64 format.

1.  The sequence of key/value pairs in the file are stored in sorted
order and partitioned into a sequence of data blocks.  These blocks
come one after another at the beginning of the file.  Each data block
is formatted according to the code in `block_builder.cc`, and then
optionally compressed.
1. 文件中 key/value 对的序列有序存储且划分成数据块序列。这些块在文件头一个一个
地排列。没一个数据块都是根据 `block_builder.cc` 中的代码来格式化, 且可选地进
行压缩。

2. After the data blocks we store a bunch of meta blocks.  The
supported meta block types are described below.  More meta block types
may be added in the future.  Each meta block is again formatted using
`block_builder.cc` and then optionally compressed.
2. 在数据块之后, 我们存储了一群元数据块。支持的元数据块类型在下面有描述。将来可能
加入更多的类型。每一个元数据块又是根据 `block_builder.cc` 来格式化的, 且是可
选地进行压缩。

3. A "metaindex" block.  It contains one entry for every other meta
block where the key is the name of the meta block and the value is a
BlockHandle pointing to that meta block.
3. 一个 “metaindex" 块。 它包含每一个其它的元数据块一个条目, key 是元数据块
的名字, 值则是一个指向块的 BlockHandle。

4. An "index" block.  This block contains one entry per data block,
where the key is a string >= last key in that data block and before
the first key in the successive data block.  The value is the
BlockHandle for the data block.
4. 一个 "index" 块。每一个数据块在块中都有一个条目, key 是 >= 数据块最后一个
key 且小于下一个数据块的第一个 key 的字符串。值则是数据块的 BlockHandle。

5. At the very end of the file is a fixed length footer that contains
the BlockHandle of the metaindex and index blocks as well as a magic number.
5. 在文件的最后有一个固定长度的 页脚, 其中包含了 metaindex 和 index 块的
BlockHandle, 同时还包含了一个魔数。 

        metaindex_handle: char[p];     // Block handle for metaindex
        index_handle:     char[q];     // Block handle for index
        padding:          char[40-p-q];// zeroed bytes to make fixed length
                                       // (40==2*BlockHandle::kMaxEncodedLength)
        magic:            fixed64;     // == 0xdb4775248b80fb57 (little-endian)

## "filter" Meta Block
## ”filter" 元数据块

If a `FilterPolicy` was specified when the database was opened, a
filter block is stored in each table.  The "metaindex" block contains
an entry that maps from `filter.<N>` to the BlockHandle for the filter
block where `<N>` is the string returned by the filter policy's
`Name()` method.
如果打开数据库的时候指定了 `FilterPolicy`, 每个表中会存储一个 filter 块。
"metaindex" 块包含一个从 `filter.<N>` 到 BlockHandle 的过滤块的映射条目。
其中 `<N>` 是过滤策略的 `Name()` 接口返回值。

The filter block stores a sequence of filters, where filter i contains
the output of `FilterPolicy::CreateFilter()` on all keys that are stored
in a block whose file offset falls within the range
过滤块存储了一系列的过滤器, 其中第 i 个过滤器包含 `FilterPolicy::CreateFilter()`
在那些文件偏移在 `[ i*base ... (i+1)*base-1 ]` 范围内的块中所有 key 的输出。

    [ i*base ... (i+1)*base-1 ]

Currently, "base" is 2KB.  So for example, if blocks X and Y start in
the range `[ 0KB .. 2KB-1 ]`, all of the keys in X and Y will be
converted to a filter by calling `FilterPolicy::CreateFilter()`, and the
resulting filter will be stored as the first filter in the filter
block.
当前, "base" 是 2KB。所以例如, 如果块 X 和 Y 块在 `[ 0KB .. 2KB-1 ]` 范围内
开始, 那么块 X 和 Y 中的所有 keys 会通过调用 `FilterPolicy::CreateFilter()`
被转换成一个过滤器。然后结果过滤器会作为第一个过滤器存在过滤块中。

The filter block is formatted as follows:
过滤块的格式如下:

    [filter 0]
    [filter 1]
    [filter 2]
    ...
    [filter N-1]

    [offset of filter 0]                  : 4 bytes
    [offset of filter 1]                  : 4 bytes
    [offset of filter 2]                  : 4 bytes
    ...
    [offset of filter N-1]                : 4 bytes

    [offset of beginning of offset array] : 4 bytes
    lg(base)                              : 1 byte

The offset array at the end of the filter block allows efficient
mapping from a data block offset to the corresponding filter.
过滤块文件后面的偏移数组使得可以高效地从一个数据块映射到对应的过滤器。

## "stats" Meta Block
## "stats" 元数据块

This meta block contains a bunch of stats.  The key is the name
of the statistic.  The value contains the statistic.
这个元数据块包含一系列的统计数据。key 是统计(项)的名字。值包含统计数据。

TODO(postrelease): record following stats.

    data size
    index size
    key size (uncompressed)
    value size (uncompressed)
    number of entries
    number of data blocks
