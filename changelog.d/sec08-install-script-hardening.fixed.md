- **Apache install scripts hardened.** The staging directory uses an
  unpredictable `mktemp -d` name with mode 0700, the SELinux marker moved
  out of shared `/tmp`, the CentOS signing key is fetched over HTTPS, and
  `version_compare` compares versions correctly. Covered by the new
  `install/test_install_scripts.sh` self-tests.
