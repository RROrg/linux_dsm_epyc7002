
require 'syno_kconfig'

describe 'CONFIG_SYNO_BTRFS_*' do
    include SynoKconfig

    # all configs with this prefix should be examined
    scope(/^CONFIG_SYNO_BTRFS_/)

    # btrfs should be a module
    it "CONFIG_BTRFS_FS=m" do
        platforms
            .verify('CONFIG_BTRFS_FS', module?)
    end

    # disabled configs
    %w[
        CONFIG_BTRFS_FS_POSIX_ACL
        CONFIG_BTRFS_FS_CHECK_INTEGRITY
        CONFIG_BTRFS_FS_RUN_SANITY_TESTS
        CONFIG_BTRFS_DEBUG
        CONFIG_BTRFS_ASSERT
        CONFIG_BTRFS_FS_REF_VERIFY
    ].each do |cfg|
        it "#{cfg} is not set" do
            platforms.verify(cfg, disabled?)
        end
    end

    # most configs should be enabled
    configs
        .select { |cfg| cfg =~ /^CONFIG_SYNO_BTRFS_/ }
        .reject { |cfg| cfg == 'CONFIG_SYNO_BTRFS_FILE_EXTENT_SYNO_FLAG' }
        .reject { |cfg| cfg == 'CONFIG_SYNO_BTRFS_DEDUPE' }
        .each do |cfg|
            it "#{cfg}=y" do
                platforms.verify(cfg, builtin?)
            end
        end

    # configs only enabled in specified platforms
    %w[
        CONFIG_SYNO_BTRFS_FILE_EXTENT_SYNO_FLAG
        CONFIG_SYNO_BTRFS_DEDUPE
    ].each do |cfg|
        it "#{cfg} is enabled for PURLEY, ICELAKED, V1000, R1000, GEMINILAKE, EPYC7002 and EPYC7002SOFS" do
            platforms
                .select { |p| p.target == :PURLEY || p.target == :ICELAKED || p.target == :GEMINILAKE || p.target == :GEMINILAKENK || p.target == :EPYC7002 || p.target == :EPYC7002SOFS || p.target == :V1000 || p.target == :V1000NK || p.target == :R1000NK }
                .verify(cfg, builtin?)
        end

        it "#{cfg} is disabled for non-PURLEY, non-ICELAKED, non-GEMINILAKE, non-EPYC7002, non-V1000, non-R1000, and non-EPYC7002SOFS" do
            platforms
                .reject { |p| p.target == :PURLEY || p.target == :ICELAKED || p.target == :GEMINILAKE || p.target == :GEMINILAKENK ||  p.target == :EPYC7002 || p.target == :EPYC7002SOFS || p.target == :V1000 || p.target == :V1000NK || p.target == :R1000NK }
                .verify(cfg, disabled?)
        end
    end
end

