
require 'rake'
require 'inifile'
require 'forwardable'
require 'platform_helper'

class Platform
    def self.prefix
        Rake.application.find_rakefile_location.last
    end

    def self.load
        PLATFORMS.each do |p|
            p.configs ||= IniFile.load("#{prefix}/../synoconfigs/#{p.abbr}").to_h['global']
        end
    rescue Exception => e
        STDERR.puts e
        raise Interrupt
    end
end

class Platform
    include Platform::Helper
    include Enumerable
    extend Forwardable

    attr_reader :abbr, :target, :family, :desc
    attr_accessor :configs
    def_delegators :configs, :[], :[]=, :each

    def initialize(abbr, target, family, desc)
        @abbr     = abbr
        @target   = target
        @family   = family
        @desc     = desc
    end

    def to_s
        @target.to_s
    end
end

# this table should be matched with the information in lnxscripts/include/platforms
PLATFORMS = [
    #             abbr               target                  family      desc
    Platform.new(:kvmx64lk5,        :KVMX64,                :x86_64,    'Virtual Machine'),
    Platform.new(:kvmx64sofs,       :KVMX64SOFS,            :x86_64,    'Virtual Machine with SOFS'),
    Platform.new(:kvmx64v2,         :KVMX64V2,              :x86_64,    'Virtual Machine with port mapping v2'),
    Platform.new(:purleylk5,        :PURLEY,                :x86_64,    'Intel Purley'),
    Platform.new(:geminilakelk5,    :GEMINILAKE,            :x86_64,    'Intel Gemini Lake'),
    Platform.new(:geminilakenk,     :GEMINILAKENK,          :x86_64,    'Intel Gemini Lake'),
    Platform.new(:v1000lk5,         :V1000,                 :x86_64,    'AMD Ryzen Embedded V1000'),
    Platform.new(:v1000nk,          :V1000NK,               :x86_64,    'AMD Ryzen Embedded V1000'),
    Platform.new(:v1000sofs,        :V1000SOFS,             :x86_64,    'AMD Ryzen Embedded V1000 with SOFS'),
    Platform.new(:rtd1619b,         :RTD1619B,              :armv8,     'Realtek RTD1619B'),
    Platform.new(:icelaked,         :ICELAKED,              :x86_64,    'Intel Icelake-D'),
    Platform.new(:epyc7002,         :EPYC7002,              :x86_64,    'AMD EPYC Embedded 7002'),
    Platform.new(:epyc7002sofs,     :EPYC7002SOFS,          :x86_64,    'AMD EPYC Embedded 7002 with SOFS'),
    Platform.new(:epyc7003ntb,      :EPYC7003NTB,           :x86_64,    'AMD EPYC Embedded 7003 with NTB'),
    Platform.new(:r1000nk,          :R1000NK,               :x86_64,    'AMD Ryzen Embedded R1000'),
]

