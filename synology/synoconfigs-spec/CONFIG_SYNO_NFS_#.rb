
require 'syno_kconfig'

describe 'CONFIG_SYNO_NFSD?_*' do
    include SynoKconfig

    # all configs with this prefix should be examined
    scope(/^CONFIG_SYNO_NFS_/)

    # kernel module
    %w[
        CONFIG_NFS_FS
        CONFIG_NFS_V2
        CONFIG_NFS_V3
        CONFIG_NFS_V4
        CONFIG_NFSD
        CONFIG_GRACE_PERIOD
        CONFIG_SUNRPC
        CONFIG_SUNRPC_GSS
        CONFIG_RPCSEC_GSS_KRB5
    ].each do |cfg|
        it "#{cfg}=m" do
            platforms.verify(cfg, module?)
        end
    end

    # built-in configs
    %w[
        CONFIG_EXPORTFS
        CONFIG_NETWORK_FILESYSTEMS
        CONFIG_NFS_USE_KERNEL_DNS
        CONFIG_NFS_DEBUG
        CONFIG_NFSD_V3
        CONFIG_NFSD_V4
        CONFIG_LOCKD_V4
        CONFIG_NFS_COMMON
        CONFIG_SUNRPC_DEBUG
    ].each do |cfg|
        it "#{cfg}=y" do
            platforms.verify(cfg, builtin?)
        end
    end

    # disabled configs
    %w[
        CONFIG_NFS_V3_ACL
        CONFIG_NFS_SWAP
        CONFIG_NFS_V4_1
        CONFIG_NFS_USE_LEGACY_DNS
        CONFIG_NFS_DISABLE_UDP_SUPPORT
        CONFIG_NFSD_V3_ACL
        CONFIG_NFSD_BLOCKLAYOUT
        CONFIG_NFSD_SCSILAYOUT
        CONFIG_NFSD_FLEXFILELAYOUT
        CONFIG_NFSD_V4_SECURITY_LABEL
        CONFIG_SUNRPC_DISABLE_INSECURE_ENCTYPES
    ].each do |cfg|
        it "#{cfg} is not set" do
            platforms.verify(cfg, disabled?)
        end
    end

    configs
        .select { |cfg| cfg =~ /^CONFIG_SYNO_NFS/ }
        .reject { |cfg| cfg =~ /_PACKET_SIZE$/ }
        .reject { |cfg| cfg == 'CONFIG_SYNO_NFSD_NUMA_SVC_POOL_PERNODE' }
        .reject { |cfg| cfg == 'CONFIG_SYNO_NFSD_POOL_HINT' }
        .each do |cfg|
            it "#{cfg}=y" do
                platforms.verify(cfg, builtin?)
            end
        end

    it "CONFIG_SYNO_NFSD_UDP_MAX_PACKET_SIZE=32768" do
        platforms
            .verify('CONFIG_SYNO_NFSD_UDP_MAX_PACKET_SIZE', equaled?(32768))
    end

    it "CONFIG_SYNO_NFSD_UDP_MIN_PACKET_SIZE=4096" do
        platforms
            .verify('CONFIG_SYNO_NFSD_UDP_MIN_PACKET_SIZE', equaled?(4096))
    end

    it "CONFIG_SYNO_NFSD_UDP_DEF_PACKET_SIZE=8192" do
        platforms
            .verify('CONFIG_SYNO_NFSD_UDP_DEF_PACKET_SIZE', equaled?(8192))
    end

    it "CONFIG_SYNO_NFSD_NUMA_SVC_POOL_PERNODE=y if CONFIG_NUMA enabled" do
        platforms
            .select { |p| p['CONFIG_NUMA'].enabled? }
            .verify('CONFIG_SYNO_NFSD_NUMA_SVC_POOL_PERNODE', enabled?)

        platforms
            .reject { |p| p['CONFIG_NUMA'].enabled? }
            .verify('CONFIG_SYNO_NFSD_NUMA_SVC_POOL_PERNODE', disabled?)
    end
end

