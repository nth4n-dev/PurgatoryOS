// rust/src/fs.rs
use alloc::boxed::Box;
use alloc::collections::BTreeMap;
use alloc::string::String;
use alloc::vec::Vec;
use core::slice;
use spin::Mutex;

/// Integer handle for a filesystem node. The kernel's equivalent of
/// an inode number. Zero is reserved; #1 is always the root.
pub type NodeId = u32;

/// Every filesystem object is a Node. Directories hold a name→NodeId
/// map; files hold a byte vector. No permissions yet; no timestamps;
/// no symlinks. Add those as separate posts if the series goes longer.
pub enum NodeKind {
    Directory(BTreeMap<String, NodeId>),
    File(Vec<u8>),
}

pub struct Node {
    pub id:    NodeId,
    pub name:  String,
    pub kind:  NodeKind,
}

/// The whole filesystem state. Single-rooted, all in-memory, one
/// BTreeMap<NodeId, Box<Node>> owning every node in the tree.
///
/// `next_id` is a monotonically-increasing counter. We never reuse
/// inode numbers. That keeps a stale fd from accidentally referring
/// to a new file if the old one is unlinked (not that we support
/// unlink yet).
pub struct Vfs {
    nodes:   BTreeMap<NodeId, Box<Node>>,
    next_id: NodeId,
}

impl Vfs {
    pub const fn empty() -> Self {
        Vfs {
            nodes:   BTreeMap::new(),
            next_id: 2,   // root is always #1
        }
    }

    /// Allocate a fresh NodeId. Never returns 0.
    fn alloc_id(&mut self) -> NodeId {
        let id = self.next_id;
        self.next_id += 1;
        id
    }

    /// Install the root directory (#1). Called exactly once from
    /// seed(); panics if called a second time.
    fn make_root(&mut self) {
        assert!(!self.nodes.contains_key(&1), "root already exists");
        let root = Node {
            id:   1,
            name: String::from("/"),
            kind: NodeKind::Directory(BTreeMap::new()),
        };
        self.nodes.insert(1, Box::new(root));
    }


    /// Create a directory named `name` inside `parent`. Returns
    /// its new NodeId.
    fn mkdir(&mut self, parent: NodeId, name: &str) -> NodeId {
        let id = self.alloc_id();
        let dir = Box::new(Node {
            id,
            name: String::from(name),
            kind: NodeKind::Directory(BTreeMap::new()),
        });
        self.nodes.insert(id, dir);
        self.link_into(parent, name, id);
        id
    }

    /// Create a file named `name` inside `parent` containing `data`.
    fn mkfile(&mut self, parent: NodeId, name: &str, data: &[u8]) -> NodeId {
        let id = self.alloc_id();
        let file = Box::new(Node {
            id,
            name: String::from(name),
            kind: NodeKind::File(Vec::from(data)),
        });
        self.nodes.insert(id, file);
        self.link_into(parent, name, id);
        id
    }

    /// Add an existing NodeId into `parent`'s children map under `name`.
    /// Invariant violation (parent not a dir, name already present) is
    /// a panic: seed() is the only caller right now and seed() is correct.
    fn link_into(&mut self, parent: NodeId, name: &str, child: NodeId) {
        let p = self.nodes.get_mut(&parent)
            .expect("link_into: parent NodeId missing");
        match &mut p.kind {
            NodeKind::Directory(children) => {
                children.insert(String::from(name), child);
            }
            NodeKind::File(_) => {
                panic!("link_into: parent is not a directory");
            }
        }
    }


    /// Walk a slash-separated path from the root. Returns the NodeId
    /// of the leaf, or None if any component is missing or not a dir
    /// on the way down.
    pub fn resolve(&self, path: &str) -> Option<NodeId> {
        let mut cur: NodeId = 1;                // start at /
        for comp in path.split('/').filter(|s| !s.is_empty()) {
            let node = self.nodes.get(&cur)?;
            let children = match &node.kind {
                NodeKind::Directory(c) => c,
                NodeKind::File(_)      => return None,
            };
            cur = *children.get(comp)?;
        }
        Some(cur)
    }

    /// Copy up to `buf.len()` bytes starting at `offset` from the file
    /// at `id`. Returns the number of bytes copied, or None if `id` is
    /// not a file.
    pub fn read(&self, id: NodeId, offset: usize, buf: &mut [u8]) -> Option<usize> {
        let node = self.nodes.get(&id)?;
        let data = match &node.kind {
            NodeKind::File(v) => v,
            NodeKind::Directory(_) => return None,
        };
        if offset >= data.len() { return Some(0); }
        let n = core::cmp::min(buf.len(), data.len() - offset);
        buf[..n].copy_from_slice(&data[offset..offset + n]);
        Some(n)
    }

    /// Copy the names of `id`'s directory entries into `dst` as a
    /// sequence of NUL-terminated strings, in BTreeMap iteration order.
    /// Returns the number of names written, or -1 if `id` is not a
    /// directory. Truncation is silent. Names that don't fit are
    /// dropped, and the caller sees a smaller count than the directory
    /// actually has.
    ///
    /// Wire format: name1\0name2\0name3\0 .... The C side walks it
    /// with a loop of strlen() calls. Cheaper across FFI than passing an
    /// array of pointers, and avoids any allocation in the readdir path.
    pub fn readdir(&self, id: NodeId, dst: &mut [u8]) -> i64 {
        let node = match self.nodes.get(&id) {
            Some(n) => n,
            None    => return -1,
        };
        let children = match &node.kind {
            NodeKind::Directory(c) => c,
            NodeKind::File(_)      => return -1,
        };

        let mut off: usize = 0;
        let mut n:   i64   = 0;
        for (name, _child_id) in children.iter() {
            let bytes = name.as_bytes();
            if off + bytes.len() + 1 > dst.len() { break; }
            dst[off..off + bytes.len()].copy_from_slice(bytes);
            dst[off + bytes.len()] = 0;
            off += bytes.len() + 1;
            n   += 1;
        }
        n
    }
}

/// The one true filesystem. A Mutex because, just like the allocator,
/// reentrancy across a timer IRQ would race on next_id / nodes.
pub static VFS: Mutex<Vfs> = Mutex::new(Vfs::empty());

/// Build the initial ramdisk. Called once from fs_init() via FFI.
pub fn seed() {
    let mut fs = VFS.lock();
    fs.make_root();

    let bin  = fs.mkdir(1, "bin");
    let etc  = fs.mkdir(1, "etc");
    let home = fs.mkdir(1, "home");

    // /bin entries
    let elf_stub = [
        0x7f, b'E', b'L', b'F',  2, 1, 1, 0,  0, 0, 0, 0,  0, 0, 0, 0,
           2, 0, 0xb7, 0,        1, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
    ];
    fs.mkfile(bin,  "sh",       &elf_stub);
    fs.mkfile(bin,  "hello",    b"hello, world!\n");

    // /etc entries
    fs.mkfile(etc,  "motd",     b"Welcome to PurgatoryOS. Your heap is a disk.\n");
    fs.mkfile(etc,  "hostname", b"purgatory\n");

    // /home entries
    fs.mkfile(home, "README",   b"This is /home. Post 13's shell will cd here on start.\n");
}
