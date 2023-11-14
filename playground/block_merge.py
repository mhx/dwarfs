'''
Problem:

- We have N distinct sources of blocks, each with an unknown number of blocks
  that will eventually be emitted.

- Only M <= N sources are active at the same time. The order in which the
  sources become active is deterministic. A new source becomes active only
  when a previously active source is finished.

- We need to find a way to output the blocks in a deterministic order while
  keeping the number of in-flight blocks to a minimum.

- If a source emits the last block, we know for sure that it's the last block.

Example:

Let's assume N=10 sources and M=4 simultaneously active sources:

  A: 2
  B: 3
  C: 9
  D: 2
  E: 5
  F: 3
  G: 3
  H: 6
  I: 1
  J: 7

Even though we don't know the order in which the blocks will be emitted,
we can determine the order in which they have to be written up front:

  A1  B1  C1  D1
  A2* B2  C2  D2*
  E1  B3* C3  F1
  E2  G1  C4  F2
  E3  G2  C5  F3*
  E4  G3* C6  H1
  E5  I1* C7  H2
  J1      C8  H3
  J2      C9* H4
  J3          H5
  J4          H6*
  J5
  J6
  J7*

The order can only be deterministic for a the same number of "slots".
The order can always be enforced while keeping at most M-1 blocks in
flight (although more is possible).

We need to know up front which sources exist and what their order is
going to be. (Not a problem, we know this.)

One issue is that we cannot re-use a slot until we know for sure that
no previous slot will become available. So we have to wait until all
previous slots have emitted a block and we know if they can be re-used
or not.

Blocks can already undergo compression while they're still waiting to
be deterministically ordered. Ordering is a completely separate process.

Essentially, it looks like the only viable strategy is to block emitters
until all blocks preceding the block they're trying to emit have been
processed.

'''

import queue
import random
import threading
import time
from numpy import random as npr

class source(object):
    def __init__(self, name):
        self.__name = name
        self.__idx = 0
        self.__num = random.randint(1, 19)

    def next(self):
        idx = self.__idx
        self.__idx += 1
        return (f"{self.__name}.{idx}", self.__idx >= self.__num, npr.exponential(scale = 0.001))

    def name(self):
        return self.__name

    def blocks(self):
        return self.__num

    def __repr__(self):
        return f"{self.__name} -> {self.__idx}/{self.__num}"

def emitter(q, merger):
    while True:
        try:
            s = q.get_nowait()
        except queue.Empty:
            break

        while True:
            block, final, wait = s.next()
            time.sleep(wait)
            merger.add(s.name(), block, final, wait)
            if final:
                break

class block_merger(object):
    def __init__(self, slots, sources):
        self.__slots = slots
        self.__sources = sources
        self.__active = []
        self.__index = 0
        self.__merged = []
        self.__blocked = 0
        self.__total_time = 0
        self.__total_wait_time = 0
        self.__cv = threading.Condition()
        while len(self.__active) < self.__slots and len(self.__sources) > 0:
            self.__active.append(self.__sources.pop(0))

    def add(self, source, block, final, wait_ONLY_FOR_DEBUGGING):
        self.__cv.acquire()
        start = time.time()
        assert source in self.__active
        ix = self.__active.index(source)
        while ix != self.__index:
            self.__blocked += 1
            self.__cv.wait()
        # print(f"{source}, {block}, {final}")
        self.__merged.append(block)
        if final:
            self.__active[ix] = self.__sources.pop(0) if len(self.__sources) > 0 else None
        while True:
            self.__index = (self.__index + 1) % self.__slots
            if self.__index == ix or self.__active[self.__index] is not None:
                break
        end = time.time()
        self.__total_wait_time += end - start
        self.__total_time += wait_ONLY_FOR_DEBUGGING
        self.__cv.notify_all()
        self.__cv.release()

    def total_time(self):
        return self.__total_time

    def total_wait_time(self):
        return self.__total_wait_time

    def blocked(self):
        return self.__blocked

    def merged(self):
        return self.__merged

class block_merger2(object):
    def __init__(self, slots, sources, max_per_slot):
        self.__slots = slots
        self.__sources = sources
        self.__source_index = {}
        self.__queue = []
        self.__index = 0
        self.__merged = []
        self.__blocked = 0
        self.__total_time = 0
        self.__total_wait_time = 0
        self.__cv = threading.Condition()
        while len(self.__source_index) < self.__slots and len(self.__sources) > 0:
            self.__source_index[self.__sources.pop(0)] = len(self.__source_index)
            self.__queue.append(queue.Queue(maxsize=max_per_slot))

    # IDEA: Have queues for each index that elements can already be
    #       added to. We need some kind of semaphore magic that will
    #       allow us to have up to a certain number of blocks in flight
    #       while at the same time making sure we can still generate
    #       the missing blocks.
    #       Simple strategy: allow up to Q blocks per slot, with Q
    #       chosen such that Q < <max-in-flight-blocks>/<slots>.

    def add(self, source, block, final, wait_ONLY_FOR_DEBUGGING):
        # self.__cv.acquire()
        start = time.time()
        assert source in self.__source_index
        ix = self.__source_index[source]
        assert self.__queue[ix] is not None
        self.__queue[ix].put((source, block, final))
        self.__try_merge()
        end = time.time()
        self.__total_wait_time += end - start
        self.__total_time += wait_ONLY_FOR_DEBUGGING
        # self.__cv.notify_all()
        # self.__cv.release()

    def __try_merge(self):
        try:
            while True:
                assert self.__queue[self.__index] is not None
                source, block, final = self.__queue[self.__index].get_nowait()
                self.__merged.append(block)
                if final:
                    del self.__source_index[source]
                    if len(self.__sources) > 0:
                        self.__source_index[self.__sources.pop(0)] = self.__index
                    else:
                        self.__queue[self.__index] = None
                old_ix = self.__index
                while True:
                    self.__index = (self.__index + 1) % self.__slots
                    if self.__index == old_ix or self.__queue[self.__index] is not None:
                        break
        except queue.Empty:
            pass

    def total_time(self):
        return self.__total_time

    def total_wait_time(self):
        return self.__total_wait_time

    def blocked(self):
        return self.__blocked

    def merged(self):
        return self.__merged

N = 100
M = 16

for run in range(10):
    ref = None
    for rep in range(10):
        sources = []
        q = queue.Queue()

        random.seed(run)

        total_blocks = 0

        for si in range(N):
            s = source(si)
            total_blocks += s.blocks()
            sources.append(s.name())
            q.put(s)

        print(f"==== {run}/{rep} [{total_blocks}] =============")

        merger = block_merger2(slots=M, sources=sources, max_per_slot=1)
        threads = []

        random.seed()

        t0 = time.time()

        for ei in range(M):
            t = threading.Thread(target=emitter, args=(q, merger))
            threads.append(t)
            t.start()

        for t in threads:
            t.join()

        t1 = time.time()

        elapsed = len(threads)*(t1 - t0)
        efficiency = merger.total_time() / elapsed

        print(f"====> {merger.blocked():.3f} / {merger.total_time():.3f} / {merger.total_wait_time():.3f} / {elapsed:.3f} / {efficiency*100:.1f}%")

        if ref is None:
            ref = merger.merged()
            assert len(ref) == total_blocks
            # print(f"{ref}")
        elif ref != merger.merged():
            print(f"{merger.merged()}")
            assert False
