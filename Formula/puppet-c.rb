class PuppetC < Formula
  desc "Native Puppet language compiler and facter written in C"
  homepage "https://github.com/ppomes/puppet_c"
  head "https://github.com/ppomes/puppet_c.git", branch: "master"
  license "Apache-2.0"

  depends_on "autoconf" => :build
  depends_on "automake" => :build
  depends_on "libtool" => :build
  depends_on "pkg-config" => :build

  depends_on "libyaml"
  depends_on "openssl@3"
  depends_on "ruby@3.3"
  depends_on "tree-sitter"

  # Only the compiler and facter; server/agent need additional deps
  # depends_on "libmicrohttpd"  # for puppetc-server
  # depends_on "curl"            # for puppetc-agent
  # depends_on "sqlite"          # for puppetdb support

  def install
    system "autoreconf", "-fi"

    # Point configure at Homebrew's keg-only dependencies
    ruby = Formula["ruby@3.3"]
    openssl = Formula["openssl@3"]

    # Find Ruby include dirs via rbconfig
    ruby_bin = "#{ruby.opt_bin}/ruby"
    ruby_hdrdir = Utils.safe_popen_read(ruby_bin, "-e", 'puts RbConfig::CONFIG["rubyhdrdir"]').strip
    ruby_archdir = Utils.safe_popen_read(ruby_bin, "-e", 'puts RbConfig::CONFIG["archdir"]').strip
    ruby_libdir = Utils.safe_popen_read(ruby_bin, "-e", 'puts RbConfig::CONFIG["libdir"]').strip
    ruby_lib = Utils.safe_popen_read(ruby_bin, "-e", 'puts RbConfig::CONFIG["RUBY_SO_NAME"]').strip

    system "./configure", "--prefix=#{prefix}",
                          "--with-openssl=#{openssl.opt_prefix}",
                          "--with-yaml=#{Formula["libyaml"].opt_prefix}",
                          "--with-treesitter=#{Formula["tree-sitter"].opt_prefix}",
                          "--with-ruby=#{ruby.opt_prefix}",
                          # Disable server/agent deps we don't need
                          "RUBY_CFLAGS=-I#{ruby_hdrdir} -I#{ruby_archdir}",
                          "RUBY_LIBS=-L#{ruby_libdir} -l#{ruby_lib}"

    # Build only common, facter, and compiler (skip server/agent that need extra deps)
    system "make", "-C", "common"
    system "make", "-C", "facter"
    system "make", "-C", "compiler"

    # Install binaries
    bin.install "compiler/.libs/puppetc-compile"
    bin.install "facter/.libs/facter_c"

    # Install libraries
    lib.install Dir["compiler/.libs/libpuppetc.#{OS.mac? ? "dylib" : "so"}*"]
    lib.install Dir["common/.libs/libpuppetc_common.#{OS.mac? ? "dylib" : "so"}*"]
    lib.install Dir["facter/.libs/libfacter_c.#{OS.mac? ? "dylib" : "so"}*"]

    # Install headers
    (include/"puppetc").install Dir["include/*.h"]
  end

  test do
    # Verify binaries run
    assert_match "puppet-c", shell_output("#{bin}/puppetc-compile --help 2>&1", 1)
    assert_match version.to_s, shell_output("#{bin}/facter_c 2>&1")
  end
end
