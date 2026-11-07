"""Strict tiny chain fixture for publication-journal lifecycle tests."""
class ChainRPC:
    def __init__(self):self.chain={0:'0'*64,1:'1'*64};self.calls=[]
    def fork(self):self.chain={0:'0'*64,1:'2'*64}
    def extend(self):self.chain[2]='3'*64
    def call(self,method,parameters=None,**kwargs):
        self.calls.append((method,parameters))
        if method=='getbestblockhash':return self.chain[max(self.chain)]
        if method=='getblockhash':return self.chain[parameters[0]]
        if method=='getblockheader':
            matches=[height for height,block in self.chain.items()if block==parameters[0]]
            if not matches:raise ValueError('Unknown fixture block')
            return dict(height=matches[0])
        if method=='getpgnworkview':
            return dict(schema='PGN_WORK_VIEW_V1',kind='audits',request_id=parameters[3],
                        state=dict(genesis=self.chain[0],height=max(self.chain),bestblockhash=getattr(self,'view_tip',self.chain[max(self.chain)])),
                        audits=[dict(request_id=parameters[3],jobs=[dict(slot=0,attempt=0,state='passed')])])
        raise AssertionError(method)
