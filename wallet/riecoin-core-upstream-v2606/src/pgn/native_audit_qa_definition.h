// Synthetic QA definition, selected only by the separate QA executable.
#pragma once
#include <pgn/testnet_definition.h>
#include <pgn/native_audit_qa_identity.h>
namespace pgn::core {
inline NativeMainParameterDefinition BuildPgnAuditQaDefinition(){
 auto d=BuildPgnTestnetDefinition();CMutableTransaction cb{*d.genesis.vtx.front()};
 const std::string label="PGN AUDIT QA R180 R191 / synthetic only / no public activation";
 cb.vin[0].scriptSig=CScript{}<<OP_0<<Bytes{label.begin(),label.end()};
 const auto target=*CanonicalBootstrapHashTarget(d.genesis.nBits);
 for(uint64_t counter=0;;++counter){
  if(counter>1000000)throw std::logic_error("QA genesis counter exhausted");
  cb.vin[0].scriptSig=CScript{}<<OP_0<<Bytes{label.begin(),label.end()}<<int64_t(counter);
  d.genesis.vtx[0]=MakeTransactionRef(cb);d.genesis.hashMerkleRoot=BlockMerkleRoot(d.genesis);
  if(UintToArith256(d.genesis.GetHash())<=target)break;
 }
 d.pinned_genesis=d.genesis.GetHash();if(d.pinned_genesis!=NativeAuditQaGenesis())throw std::logic_error("QA genesis definition changed");d.magic={0x70,0x67,0x6e,0x71};d.peer_port=48951;d.rpc_port=48952;d.addresses.hrp="qpgn";d.addresses.keyhash_alias.reset();
 return d;
}
}
