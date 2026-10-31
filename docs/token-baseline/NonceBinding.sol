// SPDX-License-Identifier: MIT OR Apache-2.0
pragma solidity ^0.8.24;

// The Solidity side of the token baseline (docs/TOKEN-BASELINE.md): verify
// and the nonce binding, the same steps as acceptAt in
// examples/graded/5-sign-verify-nonce.lang. The XMSS verify is a deployed
// library contract, as verifyAt is a base operation in leaf-lang.
// Counted, not built.

/// The XMSS public key: the root of the tree and the public seed.
struct PubKey {
    bytes32 root;
    bytes32 pubSeed;
}

/// An XMSS signature at its height: the leaf index, the WOTS+ chains and
/// the authentication path.
struct Sig {
    uint8 height;
    uint32 index;
    bytes32[] wots;
    bytes32[] auth;
}

interface IXmssVerifier {
    function verifyAt(uint8 height, PubKey calldata pk, bytes32 message, Sig calldata sig)
        external
        view
        returns (bool);
}

contract NonceBinding {
    IXmssVerifier private immutable xmss;

    constructor(IXmssVerifier xmss_) {
        xmss = xmss_;
    }

    /// True when the leaf index of sig is the nonce k and verifyAt is true.
    /// The index check comes first, thus a wrong nonce does no hash work.
    function acceptAt(PubKey calldata pk, uint32 k, bytes32 message, Sig calldata sig)
        external
        view
        returns (bool)
    {
        return sig.index == k && xmss.verifyAt(sig.height, pk, message, sig);
    }
}
