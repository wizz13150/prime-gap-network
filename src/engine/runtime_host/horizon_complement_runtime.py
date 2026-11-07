"""Explicit, bounded integration of the qualified native complementary worker.

This object is supplied by preparation, never selected as a fallback. It admits
only its proved residual scope and qualified SM86/K64 image. It makes no network
admission or 95-bit completeness claim.
"""
from pathlib import Path
from horizon_complement_feed import ComplementFeed,ComplementFeedBank,publish_pages
from pgn_native_contract import module_metadata,file_sha
from horizon_scientific_identity import IMAGE_SHA,SOURCE_SHA,TABLE_SHA,QUALIFIED_VARIANTS,qualified_complement_pair,qualified_complement_architecture


def qualified_inputs(worker,table,architecture):
    if type(architecture)is not int or architecture not in (86,120):
        raise ValueError('No qualified complementary worker for this architecture')
    worker=Path(worker).resolve(strict=True);table=Path(table).resolve(strict=True)
    metadata=module_metadata(worker)
    if (qualified_complement_architecture(metadata['image_sha256'],metadata['source_manifest_sha256'])!=architecture
            or type(metadata['profile_k'])is not int or metadata['profile_k']!=64
            or type(metadata['build_arch'])is not int or metadata['build_arch']!=architecture
            or metadata.get('resident_job_schema')!='SCOUT_RESIDENT_JOB_V1'
            or file_sha(table)!=TABLE_SHA):
        raise ValueError('Complement input/image differs from the qualified contract')
    return worker,table,metadata


class ComplementRuntime:
    maximum_piece=10**12

    def __init__(self,worker,table,feed,cache,architecture):
        if not isinstance(feed,(ComplementFeed,ComplementFeedBank)):
            raise ValueError('A qualified complement with a bound feed is required')
        self.worker,self.table,metadata=qualified_inputs(worker,table,architecture)
        self.feed=feed;self.cache=Path(cache)
        self.feeds=feed if isinstance(feed,ComplementFeedBank)else ComplementFeedBank((feed,))
        self.image_sha=metadata['image_sha256']

    def workers(self):return [(64,self.worker)]

    def bind_configuration(self,configuration):
        a,b=int(configuration['start']),int(configuration['end'])
        selected=self.feeds.select(a,b)
        return dict(configuration,complement_feed_sha256=selected.fingerprint)

    def plan(self,command):
        self._command(command)
        return [*command,*self._mode()]

    def launch(self,command,native_positions):
        self._command(command)
        if type(native_positions)is not int or native_positions<0:raise ValueError('Exact native progress required')
        start=int(command[command.index('--start')+1]);length=int(command[command.index('--length')+1])
        if native_positions>=length:raise ValueError('Complement resume must retain unfinished owned positions')
        selected=self.feeds.select(start+native_positions,start+length)
        folder=publish_pages(selected,self.cache,start+native_positions,length-native_positions)
        return [*command,*self._mode(),'--composite-page-directory',str(folder)]

    def _mode(self):
        return ['--composite-small-factors','--composite-canonical-table',str(self.table)]

    def _command(self,command):
        if (not command or Path(command[0]).resolve()!=self.worker
                or any(arg.startswith('--composite-')for arg in command[1:])):
            raise ValueError('Complement command/image was already overridden')
        if file_sha(self.worker)!=self.image_sha:raise ValueError('Complement image changed before execution')


class DeferredComplementRuntime(ComplementRuntime):
    """Dense primary now; complete residual repair must precede any sealing.

    No empty/partial factor page is presented as a prepared source. The same
    qualified worker runs its explicit dense-only mode. The resolver executes
    on the off-GPU finalization lane and must return a bound PreparedSourcePlan.
    """
    requires_preseal=True
    # Dense jobs do not publish inline residual pages. A complete4T logical
    # range was qualified against four1T jobs with identical full retained
    # pairs and final reports; keep the larger bound specific to this mode.
    maximum_piece=4*10**12

    def __init__(self,worker,table,architecture,source_resolver):
        if not callable(source_resolver):raise ValueError('Exact prepared source resolver required')
        self.worker,self.table,metadata=qualified_inputs(worker,table,architecture)
        self.image_sha=metadata['image_sha256'];self.source_sha=metadata['source_manifest_sha256']
        self.source_resolver=source_resolver

    def bind_configuration(self,configuration):
        a,b=int(configuration['start']),int(configuration['end'])
        if (configuration['start']!=str(a)or configuration['end']!=str(b)
                or not (1<<66)<=a<b<=10**21):
            raise ValueError('Deferred residual work escapes the proved supported domain')
        return dict(configuration,complement_source_mode='DEFERRED_RESIDUAL_PRESEAL_V1',
                    complement_primary_image_sha256=self.image_sha,
                    complement_primary_source_sha256=self.source_sha,
                    complement_canonical_table_sha256=TABLE_SHA,preseal_required=True)

    def launch(self,command,native_positions):
        self._command(command)
        if type(native_positions)is not int or not 0<=native_positions<int(command[command.index('--length')+1]):
            raise ValueError('Exact unfinished native progress required')
        return [*command,*self._mode()]

    def prepared_source_for(self,configuration):
        from horizon_preparation_provider import PreparedSourcePlan
        source=self.source_resolver(configuration)
        if not isinstance(source,PreparedSourcePlan):raise ValueError('Bound prepared source required before sealing')
        return source

    def register_preparation(self,tickets):
        register=getattr(self.source_resolver,'register',None)
        if callable(register):register(tickets)

    def acknowledge_preparation(self,ticket):
        acknowledge=getattr(self.source_resolver,'acknowledge',None)
        if callable(acknowledge):acknowledge(ticket)

    def close(self):
        close=getattr(self.source_resolver,'close',None)
        if callable(close)and close()is False:
            raise RuntimeError('Owned scientific preparation has not closed; work retained')
