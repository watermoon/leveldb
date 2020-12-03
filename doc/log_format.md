leveldb Log format
==================
The log file contents are a sequence of 32KB blocks.  The only exception is that
the tail of the file may contain a partial block.
日志文件的内容是一系列的 32KB 块。唯一的例外是文件的末尾可能包含一个部分的(不完整的)块。

Each block consists of a sequence of records:
每一个块包含一系列的记录:

    block := record* trailer?
    record :=
      checksum: uint32     // crc32c of type and data[] ; little-endian
      length: uint16       // little-endian
      type: uint8          // One of FULL, FIRST, MIDDLE, LAST
      data: uint8[length]

A record never starts within the last six bytes of a block (since it won't fit).
Any leftover bytes here form the trailer, which must consist entirely of zero
bytes and must be skipped by readers.
一个记录永远不会在一个块的最后六个字节之内开始(因为不会匹配, 因为 checksum + length 已经
六个字节, 还有类型一个字节, 所以最少得有七个字节)。任何这里残留的字节都会作为尾部,
必须全部都是 0, 且必须被阅读器跳过。

Aside: if exactly seven bytes are left in the current block, and a new non-zero
length record is added, the writer must emit a FIRST record (which contains zero
bytes of user data) to fill up the trailing seven bytes of the block and then
emit all of the user data in subsequent blocks.
此外: 如果刚好有七个字节留在当前块, 并且一个新的非零长度的记录被添加, 写入者必须先生成(emit)
一个 FIRST 记录(零长度的用户数据)来填充当前块的最后七个字节, 然后在接下来的块中记录所有的用户
数据。

More types may be added in the future.  Some Readers may skip record types they
do not understand, others may report that some data was skipped.
将来可能会增加更多的类型。一些阅读器可以跳过哪些它们不认识的记录类型, 也可以上报那些被跳过的数据。

    FULL == 1
    FIRST == 2
    MIDDLE == 3
    LAST == 4

The FULL record contains the contents of an entire user record.
FULL 记录包含一个完整的用户记录。

FIRST, MIDDLE, LAST are types used for user records that have been split into
multiple fragments (typically because of block boundaries).  FIRST is the type
of the first fragment of a user record, LAST is the type of the last fragment of
a user record, and MIDDLE is the type of all interior fragments of a user
record.
FIRST, MIDDLE, LAST 用于表示用户记录被分割成了多个分片(通常是由于块边界)。FIRST 是用户记
录的第一个分片, LAST 是最后一个分片, MIDDLE 则是内部所有的分片。

Example: consider a sequence of user records:
举例说明: 考虑一个用户记录的序列：

    A: length 1000
    B: length 97270
    C: length 8000

**A** will be stored as a FULL record in the first block.
**A** 会以 FULL 记录的形式存储在第一个块中。

**B** will be split into three fragments: first fragment occupies the rest of
the first block, second fragment occupies the entirety of the second block, and
the third fragment occupies a prefix of the third block.  This will leave six
bytes free in the third block, which will be left empty as the trailer.
**B** 会被分割成三个分片: 第一个分片占据了第一个块的剩余空间, 第二个分片占据整个第二个块,
第三个分片占据第三个块的前面部分。这会在第三个块留下六个字节的空间, 这将会留空(0)作为尾部。

**C** will be stored as a FULL record in the fourth block.
**C** 会以 FULL 记录存储在第四个块。

----

## Some benefits over the recordio format:
## recordio 格式的一些好处

1. We do not need any heuristics for resyncing - just go to next block boundary
   and scan.  If there is a corruption, skip to the next block.  As a
   side-benefit, we do not get confused when part of the contents of one log
   file are embedded as a record inside another log file.
1. 我们不需要任何探索来做重同步(resyncing) - 只需要继续到下一个记录的边界和扫描即可。如果
存在一个数据损坏, 跳到下一个块。作为一个额外的好处, 当一个日志文件的部分内容以一个记录的形式
嵌入到另外一个记录文件的时候, 我们不会感到混乱。

2. Splitting at approximate boundaries (e.g., for mapreduce) is simple: find the
   next block boundary and skip records until we hit a FULL or FIRST record.
2. 在大概的边界分片(例如对于 mapreduce)比较简单: 找到下一个块的边界, 跳过记录直到遇到了一个
FULL 或者 FIRST 记录。

3. We do not need extra buffering for large records.
3. 我们不需要为大的记录准备额外的缓存区。

## Some downsides compared to recordio format:
## 相对的 recordio 格式的不足

1. No packing of tiny records.  This could be fixed by adding a new record type,
   so it is a shortcoming of the current implementation, not necessarily the
   format.
1. 小记录没有压紧(packing)。这可以通过添加一个新的记录类型来解决。所以这是当前实现的一个不足,
而不是格式的问题。(诡辩之才)

2. No compression.  Again, this could be fixed by adding new record types.
2. 没有压缩。再一次, 这个可以通过添加一个新的记录类型来解决。
