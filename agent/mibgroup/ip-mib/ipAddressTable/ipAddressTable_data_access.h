/*
 * Note: this file originally auto-generated by mib2c using
 *       version : 1.10 $ of : mfd-data-access.m2c,v $
 *
 * $Id$
 */
#ifndef IPADDRESSTABLE_DATA_ACCESS_H
#define IPADDRESSTABLE_DATA_ACCESS_H

#ifdef __cplusplus
extern          "C" {
#endif


    /*
     *********************************************************************
     * function declarations
     */

    /*
     *********************************************************************
     * Table declarations
     */
/**********************************************************************
 **********************************************************************
 ***
 *** Table ipAddressTable
 ***
 **********************************************************************
 **********************************************************************/
    /*
     * ipAddressTable is subid 34 of ip.
     * Its status is Current.
     * OID: .1.3.6.1.2.1.4.34, length: 8
     */


    int            
        ipAddressTable_init_data(ipAddressTable_registration_ptr
                                 ipAddressTable_reg);


    void            ipAddressTable_container_init(netsnmp_container **
                                                  container_ptr_ptr,
                                                  netsnmp_cache * cache);
    int             ipAddressTable_cache_load(netsnmp_container *
                                              container);
    void            ipAddressTable_cache_free(netsnmp_container *
                                              container);

    int             ipAddressTable_row_prep(ipAddressTable_rowreq_ctx *
                                            rowreq_ctx);


#ifdef __cplusplus
};
#endif

#endif                          /* IPADDRESSTABLE_DATA_ACCESS_H */
