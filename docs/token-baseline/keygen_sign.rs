// SPDX-License-Identifier: MIT OR Apache-2.0
//! The Rust side of the token baseline (docs/TOKEN-BASELINE.md): keygen and
//! sign, the same steps as ks4, pk4 and sig in
//! examples/graded/5-sign-verify-nonce.lang. The XMSS keygen and sign come
//! from a library crate, as keygen and sign are base operations in
//! leaf-lang. The keystore commits the next leaf index to disk before the
//! leaf signs, thus a crash never signs twice with one leaf.
//! Counted, not built.

use std::fs::File;
use std::io::{self, Write};
use std::path::Path;

use xmss::{Digest, Params, PublicKey, SecretKey, Signature};

/// The secret key and the index of the next free leaf.
pub struct Keystore {
    secret: SecretKey,
    next: u32,
}

#[derive(Debug)]
pub enum Error {
    Io(io::Error),
    Exhausted,
}

impl From<io::Error> for Error {
    fn from(e: io::Error) -> Self {
        Error::Io(e)
    }
}

pub fn keygen(sk_seed: Digest, sk_prf: Digest, pub_seed: Digest, height: u8) -> Keystore {
    Keystore {
        secret: SecretKey::generate(sk_seed, sk_prf, pub_seed, Params::v1(height)),
        next: 0,
    }
}

impl Keystore {
    pub fn public_key(&self) -> PublicKey {
        self.secret.public_key()
    }

    /// Writes and syncs the index after this leaf to `path`, then signs
    /// `msg` with this leaf. No free leaf gives `Error::Exhausted`.
    pub fn sign(&mut self, path: &Path, msg: &Digest) -> Result<Signature, Error> {
        let index = self.next;
        let next = (index < self.secret.leaf_count())
            .then(|| index + 1)
            .ok_or(Error::Exhausted)?;
        let mut file = File::create(path)?;
        file.write_all(&next.to_le_bytes())?;
        file.sync_all()?;
        self.next = next;
        Ok(self.secret.sign_at(index, msg))
    }
}

fn main() -> Result<(), Error> {
    let sk_seed = Digest::from_words([9, 10, 11, 12]);
    let sk_prf = Digest::from_words([5, 6, 7, 8]);
    let pub_seed = Digest::from_words([1, 2, 3, 4]);
    let msg = Digest::from_words([13, 14, 15, 16]);
    let mut ks4 = keygen(sk_seed, sk_prf, pub_seed, 4);
    let pk4 = ks4.public_key();
    let sig = ks4.sign(Path::new("keystore.next"), &msg)?;
    println!("{pk4:?} {sig:?}");
    Ok(())
}
