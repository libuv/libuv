
# Project Maintainers

libuv is currently managed by the following individuals:

* **Ben Noordhuis** ([@bnoordhuis](https://github.com/bnoordhuis))
  - GPG key: 46AB89B9 (pubkey-bnoordhuis)
* **Bert Belder** ([@piscisaureus](https://github.com/piscisaureus))
* **Fedor Indutny** ([@indutny](https://github.com/indutny))
  - GPG key: 19B7E890 (pubkey-indutny)
* **Saúl Ibarra Corretgé** ([@saghul](https://github.com/saghul))
  - GPG key: AE9BC059 (pubkey-saghul)

## Storing a maintainer key in Git

It's quite handy to store a maintainer's signature as a git blob, and have
that object tagged and signed with such key.

Export your public key:

    $ gpg --armor --export saghul@gmail.com > saghul.asc

Store it as a blob on the repo:

    $ git hash-object -w saghul.asc

The previous command returns a hash, copy it. For the sake of this explanation,
we'll assume it's 'abcd1234'. Storing the blob in git is not enough, it could
be garbage collected since nothing references it, so we'll create a tag for it:

    $ git tag -s pubkey-saghul abcd1234

Commit the changes and push:

    $ git push origin pubkey-saghul

