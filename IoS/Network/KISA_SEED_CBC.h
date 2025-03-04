/**
@file KISA_SEED_CBC.h
@brief SEED CBC ��ȣ �˰���
@author Copyright (c) 2013 by KISA
@remarks http://seed.kisa.or.kr/
*/
//#include <stdio.h>
//#include <stdbool.h>
//#include <string>
//#include <iostream>


#ifndef SEED_CBC_H
#define SEED_CBC_H

#ifdef  __cplusplus
extern "C" {
#endif

#ifndef OUT
#define OUT
#endif

#ifndef IN
#define IN
#endif

#ifndef INOUT
#define INOUT
#endif

    //typedef unsigned int        uint32_t;
    //typedef unsigned short      uint16_t;
    //typedef unsigned char       uint8_t;

#ifndef _KISA_ENC_DEC_
#define _KISA_ENC_DEC_
    typedef enum _SEED_ENC_DEC
    {
        KISA_DECRYPT,
        KISA_ENCRYPT,
    }KISA_ENC_DEC;
#endif

#ifndef _KISA_SEED_KEY_
#define _KISA_SEED_KEY_
    typedef struct kisa_seed_key_st
    {
        uint32_t key_data[32];
    } KISA_SEED_KEY;
#endif

#ifndef _KISA_SEED_INFO_
#define _KISA_SEED_INFO_
    typedef struct kisa_seed_info_st
    {
        KISA_ENC_DEC	encrypt;
        uint32_t			ivec[4];
        KISA_SEED_KEY	seed_key;
        uint32_t			cbc_buffer[4];
        int				buffer_length;
        uint32_t			cbc_last_block[4];
        int				last_block_flag;
    } KISA_SEED_INFO;
#endif

    /**
    @brief BYTE �迭�� int �迭�� ��ȯ�Ѵ�.
    @param in :��ȯ�� BYTE ������
    @param nLen : ��ȯ�� BYTE �迭 ����
    @return ���ڷ� ���� BYTE �迭�� int�� ��ȯ�� �����͸� ��ȯ�Ѵ�. (���������� malloc������ free�� �� ����� �Ѵ�)
    @remarks ���������� ������ ����� �Լ��� SEED CTR, CBC, HIGHT CTR, CBC�� ������ include ��
    ���� �Լ��� ��� �浹 ������ �ڿ� ������ �� �ֵ��� ���带 ���δ�.
    */
    uint32_t* chartoint32_for_SEED_CBC(IN uint8_t *in, IN int nLen);

    /**
    @brief int �迭�� BYTE �迭�� ��ȯ�Ѵ�.
    @param in :��ȯ�� int ������
    @param nLen : ��ȯ�� int �迭 ����
    @return ���ڷ� ���� int �迭�� char�� ��ȯ�� �����͸� ��ȯ�Ѵ�. (���������� malloc������ free�� �� ����� �Ѵ�)
    @remarks ���������� ������ ����� �Լ��� SEED CTR, CBC, HIGHT CTR, CBC�� ������ include ��
    ���� �Լ��� ��� �浹 ������ �ڿ� ������ �� �ֵ��� ���带 ���δ�.
    */
    uint8_t* int32tochar_for_SEED_CBC(uint32_t *in, int nLen);


    /**
    @brief SEED CBC �˰��� �ʱ�ȭ �Լ�
    @param pInfo : CBC ���ο��� ���Ǵ� ����ü�ν� ������ �����ϸ� �ȵȴ�.(�޸� �Ҵ�Ǿ� �־�� �Ѵ�.)
    @param enc : ��ȣȭ �� ��ȣȭ ��� ����
    @param pbszUserKey : ����ڰ� �����ϴ� �Է� Ű(16 BYTE)
    @param pbszIV : ����ڰ� �����ϴ� �ʱ�ȭ ����(16 BYTE)
    @return 0: pInfo �Ǵ� pbszUserKey �Ǵ� pbszIV �����Ͱ� nullptr�� ���,
    1: ����
    */
    int SEED_CBC_init(OUT KISA_SEED_INFO *pInfo, IN KISA_ENC_DEC enc, IN uint8_t *pbszUserKey, IN uint8_t *pbszIV);

    /**
    @brief SEED CBC ���� �� ��ȣȭ/��ȣȭ �Լ�
    @param pInfo : SEED_CBC_init ���� ������ KISA_HIGHT_INFO ����ü
    @param in : ��/��ȣ�� ( ���� chartoint32_for_SEED_CBC�� ����Ͽ� int�� ��ȯ�� ������)
    @param inLen : ��/��ȣ�� ����(BYTE ����)
    @param out : ��/��ȣ�� ����
    @param outLen : ����� ��/��ȣ���� ����(BYTE ������ �Ѱ��ش�)
    @return 0: inLen�� ���� 0���� ���� ���, KISA_SEED_INFO ����ü�� in, out�� �� �����Ͱ� �Ҵ�Ǿ��� ���
    1: ����
    */
    int SEED_CBC_Process(OUT KISA_SEED_INFO *pInfo, IN uint32_t *in, IN int inLen, OUT uint32_t *out, OUT int *outLen);

    /**
    @brief SEED CBC ���� ���� �� �е� ó��(PKCS7)
    @param pInfo : SEED_CBC_Process �� ��ģ KISA_HIGHT_INFO ����ü
    @param out : ��/��ȣ�� ��� ����
    @param outLen : ��� ���ۿ� ����� ������ ����(BYTE ������ �򹮱���)
    @return
    - 0 :  inLen�� ���� 0���� ���� ���,
    KISA_SEED_INFO ����ü�� out�� �� �����Ͱ� �Ҵ�Ǿ��� ���
    - 1 :  ����
    @remarks �е� ���������� 16����Ʈ ������ ó�������� ��ȣȭ �� ��� ���۴�
    �򹮺��� 16����Ʈ Ŀ�� �Ѵ�.(���� 16����Ʈ �� �� �е� ����Ÿ�� 16����Ʈ�� ����.)
    */
    int SEED_CBC_Close(OUT KISA_SEED_INFO *pInfo, IN uint32_t *out, IN int *outLen);

    /**
    @brief ó���ϰ��� �ϴ� �����Ͱ� ���� ��쿡 ���
    @param pbszUserKey : ����ڰ� �����ϴ� �Է� Ű(16 BYTE)
    @param pszbIV : ����ڰ� �����ϴ� �ʱ�ȭ ����(16 BYTE)
    @param pbszPlainText : ����� �Է� ��
    @param nPlainTextLen : �� ����(BYTE ������ �򹮱���)
    @param pbszCipherText : ��ȣ�� ��� ����
    @return ��ȣȭ�� ����� ����(char ����)
    @remarks �е� ���������� 16����Ʈ ������ ó�������� pbszCipherText�� �򹮺��� 16����Ʈ Ŀ�� �Ѵ�.
    (���� 16����Ʈ �� �� �е� ����Ÿ�� 16����Ʈ�� ����.)
    */
    int SEED_CBC_Encrypt(IN uint8_t *pbszUserKey, IN uint8_t *pbszIV, IN uint8_t *pbszPlainText, IN int nPlainTextLen, OUT uint8_t *pbszCipherText);

    /**
    @brief ó���ϰ��� �ϴ� �����Ͱ� ���� ��쿡 ���
    @param pbszUserKey : ����ڰ� �����ϴ� �Է� Ű(16 BYTE)
    @param pszbIV : ����ڰ� �����ϴ� �ʱ�ȭ ����(16 BYTE)
    @param pbszCipherText : ��ȣ��
    @param nCipherTextLen : ��ȣ�� ����(BYTE ������ �򹮱���)
    @return ��ȣȭ�� ����� ����(char ����)
    @param pbszPlainText : �� ��� ����
    */
    int SEED_CBC_Decrypt(IN uint8_t *pbszUserKey, IN uint8_t *pbszIV, IN uint8_t *pbszCipherText, IN int nCipherTextLen, OUT uint8_t *pbszPlainText);



#ifdef  __cplusplus
}
#endif

#endif