"""Idempotent migration of the experimental direct-readback wait policy."""
BASE = '''   vws->resource_wait(vws, res->hw_res);
   return vws->ios_kvm_readback'''
PREVIOUS = '''   const char *skip_wait = getenv("IOS_KVM_READBACK_SKIP_WAIT");
   if (!skip_wait || strcmp(skip_wait, "1"))
      vws->resource_wait(vws, res->hw_res);
   return vws->ios_kvm_readback'''
CURRENT = '''   const char *skip_wait = getenv("IOS_KVM_READBACK_SKIP_WAIT");
   bool skip = skip_wait && (!strcmp(skip_wait, "1") ||
      (!strcmp(skip_wait, "marker") && access("/var/tmp/ios-kvm-readback-skip-wait", F_OK) == 0));
   if (!skip)
      vws->resource_wait(vws, res->hw_res);
   return vws->ios_kvm_readback'''


def patch(text):
    if CURRENT in text:
        if text.count(CURRENT) != 1:
            raise ValueError('Ambiguous direct-readback wait implementation')
        return text
    # Match the older experiment BEFORE the bare wait, which is also its suffix.
    for old in (PREVIOUS, BASE):
        if old in text:
            if text.count(old) != 1:
                raise ValueError('Ambiguous direct-readback wait implementation')
            return text.replace(old, CURRENT, 1)
    raise ValueError('Unexpected direct-readback wait implementation')
