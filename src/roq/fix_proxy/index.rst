.. _roq-fix-proxy:

roq-fix-proxy
=============

Purpose
-------

* Proxy for FIX Bridge
* Load-balancing (horizontal scaling)
* Authentication
* Dynamic routing of order updates


Conda
-----

* :ref:`Using Conda <tutorial-conda>`

.. tab:: Install

  .. code-block:: bash

    $ conda install \
      --channel https://roq-trading.com/conda/stable \
      roq-fix-proxy

.. tab:: Configure

  .. code-block:: bash
  
    $ cp $CONDA_PREFIX/share/roq-fix-proxy/config.toml $CONFIG_FILE_PATH
  
    # Then modify $CONFIG_FILE_PATH to match your specific configuration
  
.. tab:: Run

  .. code-block:: bash
  
    $ roq-fix-proxy \
          --name "proxy-1"
          --config_file "$CONFIG_FILE_PATH" \
          --flagfile "$FLAG_FILE"


Flags
-----

* :ref:`Using Flags <abseil-cpp>`

.. code-block:: bash

   $ roq-fix-proxy --help

.. tab:: Flags

   .. include:: flags/flags.rstinc

.. tab:: Client

   .. include:: flags/client.rstinc

.. tab:: Server

   .. include:: flags/server.rstinc

.. tab:: Auth

   .. include:: flags/auth.rstinc


Authentication
--------------

The simplest version is plain comparison on the password string.

:code:`hmac_sha256`
~~~~~~~~~~~~~~~~~~~

The connecting client must compute a nonce and pass this as :code:`Logon.raw_data`.

A signature is the base64 encoding of the HMAC/SHA256 digest (using a shared secret).
The connecting client must pass this as :code:`Logon.password`.

:code:`hmac_sha256_ts`
~~~~~~~~~~~~~~~~~~~~~~

This is the same algorithm as :code:`hmac_sha256` with the only difference being
a millisecond timestamp and a period (:code:`.`) being prepended to the nonce.

The server side can then extract the timestamp and validate against its own clock.
