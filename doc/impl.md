## Files

The implementation of leveldb is similar in spirit to the representation of a
single [Bigtable tablet (section 5.3)](http://research.google.com/archive/bigtable.html).
However the organization of the files that make up the representation is
somewhat different and is explained below.

Each database is represented by a set of files stored in a directory. There are
several different types of files as documented below:
每个数据库的文件都存在在一个目录下。如下文所示, 有数种不通的文件类型:

### Log files
### 日志文件
A log file (*.log) stores a sequence of recent updates. Each update is appended
to the current log file. When the log file reaches a pre-determined size
(approximately 4MB by default), it is converted to a sorted table (see below)
and a new log file is created for future updates.
一个日志文件 (*.log) 存储了一系列的最近更新。每一个更新都是通过追加的方式添加到当前日志文件
中。当日志文件得到一个预设的大小(默认大约是 4MB) 时, 它会被转换成一个有序表
(sorted table, 见下文), 并且创建一个新的日志文件用于后续的更新。

A copy of the current log file is kept in an in-memory structure (the
`memtable`). This copy is consulted on every read so that read operations
reflect all logged updates.
当前日志文件的一个拷贝也会以 `memtable` 的数据结构保存在内存中。每一个读操作都
会查询这个拷贝, 所以读操作反应了所有的已记录的更新。

## Sorted tables
## 有序表

A sorted table (*.ldb) stores a sequence of entries sorted by key. Each entry is
either a value for the key, or a deletion marker for the key. (Deletion markers
are kept around to hide obsolete values present in older sorted tables).
一个有序表 (*.ldb) 存储了以 key 进行进行排序的一系列条目(entry)。每一个条目只有两种
情况: key 对应的值, 或者一个删除标记。(保留删除标记是为了隐藏那些在更旧的有序表中出现的
过时的值.)

The set of sorted tables are organized into a sequence of levels. The sorted
table generated from a log file is placed in a special **young** level (also
called level-0). When the number of young files exceeds a certain threshold
(currently four), all of the young files are merged together with all of the
overlapping level-1 files to produce a sequence of new level-1 files (we create
a new level-1 file for every 2MB of data.)
有序表集合被组织成一系列的 level。从日志文件生成的有序表被放在一个特殊的**年轻**层
(也叫 level-0)。当年轻层的文件数量超过一个特定的阈值(当前是 4), 所有的年轻(层)文件将会和
所有的 level-1 的重叠(指 key 相同)文件进行合并, 并生成新的 level-1 文件 (每 2MB 的
数据会创建一个新的 level-1 文件)。

Files in the young level may contain overlapping keys. However files in other
levels have distinct non-overlapping key ranges. Consider level number L where
L >= 1. When the combined size of files in level-L exceeds (10^L) MB (i.e., 10MB
for level-1, 100MB for level-2, ...), one file in level-L, and all of the
overlapping files in level-(L+1) are merged to form a set of new files for
level-(L+1). These merges have the effect of gradually migrating new updates
from the young level to the largest level using only bulk reads and writes
(i.e., minimizing expensive seeks).
年轻层的文件中可能包含重叠的的 key。然而其他层的文件里包含的确实不重叠的 key 范围。我们来
考虑第 L（L >= 1) 层的情况:
当 level-L 的所有文件大小之和超过了 (10^L) MB (例如 level-1 10MB, level-2 100MB, ...),
level-L 中的一个文件, 和下一层 Level-(L+1) 的所有文件进行合并形成了一个新的 level-(L+1) 
文件集合。这些合并可以使得仅仅通过块读和写就能让新的更新从年轻层逐渐迁移到最大的层。(最小化
代价高昂的查找)

### Manifest
### 清单文件

A MANIFEST file lists the set of sorted tables that make up each level, the
corresponding key ranges, and other important metadata. A new MANIFEST file
(with a new number embedded in the file name) is created whenever the database
is reopened. The MANIFEST file is formatted as a log, and changes made to the
serving state (as files are added or removed) are appended to this log.
一个清单文件列出了构成每一层的有序表, 对应的 key 范围, 和其它重要的元数据。当数据库被重新
打开时, 会创建一个新的清单文件(一个新的号码会嵌在文件名中)。一个清单文件被格式化成类似于
一个日志, 服务状态的修改(例如文件被添加或者删除)都会被追加到这个日志中。
`VersionEdit`编码成一个日志记录(Record)写到 Manifest 文件中

### Current
### 当前文件

CURRENT is a simple text file that contains the name of the latest MANIFEST
file.
当前文件是一个包含了最新清单文件的文本文件。`SetCurrentFile@db/filename.cc`

### Info logs
### 信息日志

Informational messages are printed to files named LOG and LOG.old.
信息性的消息会打印到命名为 LOG 和 LOG.old 的文件中。

### Others
### 其它

Other files used for miscellaneous purposes may also be present (LOCK, *.dbtmp).
其它用于各式各样的目的的文件也可能会存在 (LOCK, *.dbtmp)。

## Level 0
## 零层 (Level 0)

When the log file grows above a certain size (4MB by default):
Create a brand new memtable and log file and direct future updates here.
当日志文件增长到一个特定的大小 (默认 4MB): 创建一个包含新的 memtable 和日志文件分支, 
后面所有的更新都到新的分支去。

In the background:

1. Write the contents of the previous memtable to an sstable.
2. Discard the memtable.
3. Delete the old log file and the old memtable.
4. Add the new sstable to the young (level-0) level.

在后台:
1. 将之前的 memtable 的内容写到一个 sstable 中
2. 丢弃 memtable
3. 删除旧的日志文件和旧的 memtable
4. 将新的 sstable 添加到年轻层 (level-0)

## Compactions
## 压缩

When the size of level L exceeds its limit, we compact it in a background
thread. The compaction picks a file from level L and all overlapping files from
the next level L+1. Note that if a level-L file overlaps only part of a
level-(L+1) file, the entire file at level-(L+1) is used as an input to the
compaction and will be discarded after the compaction.  Aside: because level-0
is special (files in it may overlap each other), we treat compactions from
level-0 to level-1 specially: a level-0 compaction may pick more than one
level-0 file in case some of these files overlap each other.
如果 level L 的大小超过的它的限制, 我们会在一个后台线程压缩它。压缩过程会在 level L
挑选一个文件, 和从下一层 L+1 挑选所有的交叠文件。注意到, 如果 lelvel-L 文件仅部分交叠
level-(L+1)的某个文件, 那么 level-(L+1)的整个文件都会作为压缩过程的输入, 并且会在压缩
后被丢弃。此外: 因为 level-0 是特殊的(这一层的文件可能会互相交叠), 我们特殊处理 level-0
到 level-1 的压缩: 一个 level-0 的压缩可能会挑选多于一个的 level-0 文件, 以防可能的
这些文件相互交叠。

A compaction merges the contents of the picked files to produce a sequence of
level-(L+1) files. We switch to producing a new level-(L+1) file after the
current output file has reached the target file size (2MB). We also switch to a
new output file when the key range of the current output file has grown enough
to overlap more than ten level-(L+2) files.  This last rule ensures that a later
compaction of a level-(L+1) file will not pick up too much data from
level-(L+2).
压缩过程将挑选的文件内容进行合并来产生一些列的 level-(L+1) 文件。在当前的输出文件达到目标
文件大小(2MB)后, 我们会转向生成一个新的 level-(L+1)文件。在新生成的文件包含的 key 范围增
长到会和超过 10 个 level-(L+2)文件交叠时, 我们也会转向生成新的输出文件。后面那条规则保证
了将来在压缩 level-(L+1) 文件时, 不会从 level-(L+2)挑选太多的文件。

The old files are discarded and the new files are added to the serving state.
旧文件会被丢弃, 新文件会加入到服务状态。

Compactions for a particular level rotate through the key space. In more detail,
for each level L, we remember the ending key of the last compaction at level L.
The next compaction for level L will pick the first file that starts after this
key (wrapping around to the beginning of the key space if there is no such
file).
对于一个特定层的压缩, 压缩会在 key 空间中循环。详细地说就是, 对于每一层 L, 我们记录上一次压缩
的结束 key。那么下一次在 level L 进行压缩时, 我们会选择在这个 key 之后的第一个文件(如果不存
在这样的文件, 那么就环绕到 key 空间的开头进行)。

Compactions drop overwritten values. They also drop deletion markers if there
are no higher numbered levels that contain a file whose range overlaps the
current key.
压缩会丢弃覆盖写的值。如果没有更高层(层级数字更大, 即更旧的)的文件 key 范围交叠的 key, 那
么这个 key 的删除标记也会被丢弃。

### Timing
### 时序(此翻译还需要考虑一下)

Level-0 compactions will read up to four 1MB files from level-0, and at worst
all the level-1 files (10MB). I.e., we will read 14MB and write 14MB.
Level-0 的压缩最多从 level-0 读 4 个 1MB 的文件, 且最差的情况读所有的 level-1 文件(10MB)。
例如, 我们会读 14MB 和写 14MB(文件)。

Other than the special level-0 compactions, we will pick one 2MB file from level
L. In the worst case, this will overlap ~ 12 files from level L+1 (10 because
level-(L+1) is ten times the size of level-L, and another two at the boundaries
since the file ranges at level-L will usually not be aligned with the file
ranges at level-L+1). The compaction will therefore read 26MB and write 26MB.
Assuming a disk IO rate of 100MB/s (ballpark range for modern drives), the worst
compaction cost will be approximately 0.5 second.
除了特殊的 level-0 压缩, 我们会从 level-L 挑一个 2MB 的文件。在最差的情况下, 这个文件会
和大约 12 个 L+1 层的文件交叠(10 因为 level-(L+1) 是 level-L 的十倍大小, 加上边界的
两个(一左一右), 因为 level-L 文件的 (key) 范围通常不会和 level-(L+1) 的对齐)。因此压缩
会读和写 26MB (文件)。假设磁盘 IO 率是 100MB/s(现代驱动的大致范围), 最差的压缩会消耗大约
0.5s。

If we throttle the background writing to something small, say 10% of the full
100MB/s speed, a compaction may take up to 5 seconds. If the user is writing at
10MB/s, we might build up lots of level-0 files (~50 to hold the 5*10MB). This
may significantly increase the cost of reads due to the overhead of merging more
files together on every read.
如果我们压缩后台写(速度)到一个较小的值, 例如全速 100MB/s 的 10%, 一次压缩可能会需要消耗 5 秒。
如果用户以 10MB/s 的速度写入(levelDB), 我们可能会产生很多的 level-0 文件 (约 50 个来保存
5 * 10MB)。由于每次读会进行文件合并, 这可能会明显地增加每次读消耗。

Solution 1: To reduce this problem, we might want to increase the log switching
threshold when the number of level-0 files is large. Though the downside is that
the larger this threshold, the more memory we will need to hold the
corresponding memtable.
解决办法 1: 为了降低这个问题, 我们可能希望在 level-0 的文件数量很大的时候增加日志切换的阈值。
尽管缺点是阈值雨大, 我们将需要越多的内存来保存对应的 memtable。

Solution 2: We might want to decrease write rate artificially when the number of
level-0 files goes up.
解决办法 2: 随着 level-0 文件数量的增长, 我们可能希望人为地降低写的速率。

Solution 3: We work on reducing the cost of very wide merges. Perhaps most of
the level-0 files will have their blocks sitting uncompressed in the cache and
we will only need to worry about the O(N) complexity in the merging iterator.
解决办法 3: 我们设法降低每一次广范围的合并消耗。可能大部分的 level-0 文件会有未压缩的块在
缓存中, 我们仅需要考虑合并迭代器中的 O(N) 复杂度。


### Number of files
### 文件数量

Instead of always making 2MB files, we could make larger files for larger levels
to reduce the total file count, though at the expense of more bursty
compactions.  Alternatively, we could shard the set of files into multiple
directories.
我们可以在更大的层级创建更大的文件而不是总是创建 2MB 大小的文件, 来降低总的文件数量, 尽管会
导致更多突发性的压缩消耗。另外, 我们也可以将文件集分片到多个目录。

An experiment on an ext3 filesystem on Feb 04, 2011 shows the following timings
to do 100K file opens in directories with varying number of files:
一次 2011 年 2 约 4 日在 ext3 文件系统上的试验显示了一个目录下不同数量的文件的 100K 次文件
打开操作的时间消耗:

| Files in directory | Microseconds to open a file |
|-------------------:|----------------------------:|
|               1000 |                           9 |
|              10000 |                          10 |
|             100000 |                          16 |

So maybe even the sharding is not necessary on modern filesystems?
所以可能实际上在现代文件系统中分片是不必要的？

## Recovery
## 恢复

* Read CURRENT to find name of the latest committed MANIFEST
* Read the named MANIFEST file
* Clean up stale files
* We could open all sstables here, but it is probably better to be lazy...
* Convert log chunk to a new level-0 sstable
* Start directing new writes to a new log file with recovered sequence#

* 读取 CURRENT 文件来找到最后提交的 MANIFEST(文件清单)的文件名
* 读取(上一步得到的)文件清单文件
* 清除陈旧的文件
* 我们可以此时打开所有的 sstables, 不过可能更好的是偷懒一下(延迟打开)
* 将日志块转换成一个新的 level-0 sstable
* 开始引导将新的写操作写入到恢复后的 sequence# 的新日志文件

## Garbage collection of files
## 文件的垃圾回收

`RemoveObsoleteFiles()` is called at the end of every compaction and at the end
of recovery. It finds the names of all files in the database. It deletes all log
files that are not the current log file. It deletes all table files that are not
referenced from some level and are not the output of an active compaction.
`RemoveObsoleteFiles()` 会在每一次压缩的最后和恢复的最后被调用。它会找到数据库的所有文件的
文件名。它会删除所有不是当前日志文件的文件。它会删除所有不被任何层级引用的和不是一次活跃压缩的输
出的的表文件。
