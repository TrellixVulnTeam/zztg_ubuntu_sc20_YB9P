/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package org.conscrypt;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.security.cert.X509Certificate;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import libcore.io.IoUtils;
import libcore.util.BasicLruCache;

/**
 * This class provides a simple interface for cert pinning.
 */
public class CertPinManager {

    private long lastModified;

    private final Map<String, PinListEntry> entries = new HashMap<String, PinListEntry>();
    private final BasicLruCache<String, String> hostnameCache = new BasicLruCache<String, String>(10);

    private boolean initialized = false;
    private static final boolean DEBUG = false;

    private final File pinFile;
    private final TrustedCertificateStore certStore;

    public CertPinManager(TrustedCertificateStore store) throws PinManagerException {
        pinFile = new File("/data/misc/keychain/pins");
        certStore = store;
    }

    /** Test only */
    public CertPinManager(String path, TrustedCertificateStore store) throws PinManagerException {
        if (path == null) {
            throw new NullPointerException("path == null");
        }
        pinFile = new File(path);
        certStore = store;
    }

    /**
     * Given a {@code hostname} and a {@code chain} this verifies that the
     * certificate chain includes certificates from the pinned list iff the
     * {@code hostname} is on the list of sites that should be pinned.
     *
     * <p>If {@code chain} doesn't include those certificates and enforcing
     * mode is enabled, then this method returns {@code false} and the
     * certificate chain validation should fail.
     */
    public boolean isChainValid(String hostname, List<X509Certificate> chain)
            throws PinManagerException {
        // lookup the entry
        final PinListEntry entry = lookup(hostname);

        // There was no entry in the pin list for this hostname.
        if (entry == null) {
            return true;
        }

        return entry.isChainValid(chain);
    }

    /**
     * Tries to initialize the cache. Will return {@code true} if the
     * initialization succeeded, or {@code false} if there is no data available.
     */
    private synchronized boolean ensureInitialized() throws PinManagerException {
        if (initialized && isCacheValid()) {
            return true;
        }

        // reread the pin file
        String pinFileContents = readPinFile();

        if (pinFileContents != null) {
            // rebuild the pinned certs
            for (String entry : getPinFileEntries(pinFileContents)) {
                try {
                    PinListEntry pin = new PinListEntry(entry, certStore);
                    entries.put(pin.getCommonName(), pin);
                } catch (PinEntryException e) {
                    log("Pinlist contains a malformed pin: " + entry, e);
                }
            }

            // clear the cache
            hostnameCache.evictAll();

            // set the last modified time
            lastModified = pinFile.lastModified();

            // we've been fully initialized and are ready to go
            initialized = true;
        }

        return initialized;
    }

    private String readPinFile() throws PinManagerException {
        try {
            return IoUtils.readFileAsString(pinFile.getPath());
        } catch (FileNotFoundException e) {
            // there's no pin list, all certs are unpinned
            return null;
        } catch (IOException e) {
            // this is unexpected, fail
            throw new PinManagerException("Unexpected error reading pin list; failing.", e);
        }
    }

    private static String[] getPinFileEntries(String pinFileContents) {
        return pinFileContents.split("\n");
    }

    private synchronized PinListEntry lookup(String hostname) throws PinManagerException {
        // Ensure we're initialized, but exit early it we couldn't initialize.
        if (!ensureInitialized()) {
            return null;
        }

        // if so, check the hostname cache
        String cn = hostnameCache.get(hostname);
        if (cn != null) {
            // if we hit, return the corresponding entry
            return entries.get(cn);
        }

        // otherwise, get the matching cn
        cn = getMatchingCN(hostname);
        if (cn != null) {
            hostnameCache.put(hostname, cn);
            // we have a matching CN, return that entry
            return entries.get(cn);
        }

        // if we got here, we don't have a matching CN for this hostname
        return null;
    }

    private boolean isCacheValid() {
        return pinFile.lastModified() == lastModified;
    }

    private String getMatchingCN(String hostname) {
        String bestMatch = "";
        for (String cn : entries.keySet()) {
            // skip shorter CNs since they can't be better matches
            if (cn.length() < bestMatch.length()) {
                continue;
            }
            // now verify that the CN matches at all
            if (isHostnameMatchedBy(hostname, cn)) {
                bestMatch = cn;
            }
        }
        return bestMatch;
    }

    /**
     * Returns true if {@code hostName} matches the name or pattern {@code cn}.
     *
     * @param hostName lowercase host name.
     * @param cn certificate host name. May include wildcards like
     *            {@code *.android.com}.
     */
    private static boolean isHostnameMatchedBy(String hostName, String cn) {
        if (hostName == null || hostName.isEmpty() || cn == null || cn.isEmpty()) {
            return false;
        }

        cn = cn.toLowerCase(Locale.US);

        if (!cn.contains("*")) {
            return hostName.equals(cn);
        }

        if (cn.startsWith("*.") && hostName.equals(cn.substring(2))) {
            return true; // "*.foo.com" matches "foo.com"
        }

        int asterisk = cn.indexOf('*');
        int dot = cn.indexOf('.');
        if (asterisk > dot) {
            return false; // malformed; wildcard must be in the first part of
                          // the cn
        }

        if (!hostName.regionMatches(0, cn, 0, asterisk)) {
            return false; // prefix before '*' doesn't match
        }

        int suffixLength = cn.length() - (asterisk + 1);
        int suffixStart = hostName.length() - suffixLength;
        if (hostName.indexOf('.', asterisk) < suffixStart) {
            return false; // wildcard '*' can't match a '.'
        }

        if (!hostName.regionMatches(suffixStart, cn, asterisk + 1, suffixLength)) {
            return false; // suffix after '*' doesn't match
        }

        return true;
    }

    private static void log(String s, Exception e) {
        if (DEBUG) {
            System.out.println("PINFILE: " + s);
            if (e != null) {
                e.printStackTrace();
            }
        }
    }
}
