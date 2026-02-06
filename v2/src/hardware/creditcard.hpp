// ViewTouch V2 - Credit Card Processing System
// Modern C++/Qt6 reimplementation

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QDateTime>
#include <QJsonObject>

namespace vt2 {

//=============================================================================
// Card Types and Transaction Types
//=============================================================================
enum class CardType {
    Unknown,
    Visa,
    MasterCard,
    Amex,
    Discover,
    DinersClub,
    JCB,
    Debit,
    GiftCard
};

enum class TransactionType {
    Sale,
    AuthOnly,
    Capture,
    Void,
    Refund,
    Adjustment,
    BatchClose
};

enum class TransactionStatus {
    Pending,
    Approved,
    Declined,
    Error,
    Voided,
    Refunded,
    Timeout
};

enum class EntryMethod {
    Swipe,
    Chip,
    Contactless,
    Manual,
    Keyed
};

//=============================================================================
// Credit Card Transaction
//=============================================================================
class CreditCardTransaction : public QObject {
    Q_OBJECT
    
public:
    explicit CreditCardTransaction(QObject* parent = nullptr);
    
    // Transaction identifiers
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    QString referenceNumber() const { return m_referenceNumber; }
    void setReferenceNumber(const QString& ref) { m_referenceNumber = ref; }
    
    QString authCode() const { return m_authCode; }
    void setAuthCode(const QString& code) { m_authCode = code; }
    
    QString processorTransactionId() const { return m_processorTransactionId; }
    void setProcessorTransactionId(const QString& id) { m_processorTransactionId = id; }
    
    // Transaction details
    TransactionType transactionType() const { return m_transactionType; }
    void setTransactionType(TransactionType t) { m_transactionType = t; }
    
    TransactionStatus status() const { return m_status; }
    void setStatus(TransactionStatus s) { m_status = s; }
    
    int amount() const { return m_amount; }  // In cents
    void setAmount(int cents) { m_amount = cents; }
    
    int tipAmount() const { return m_tipAmount; }
    void setTipAmount(int cents) { m_tipAmount = cents; }
    
    int totalAmount() const { return m_amount + m_tipAmount; }
    
    // Card info (masked)
    CardType cardType() const { return m_cardType; }
    void setCardType(CardType t) { m_cardType = t; }
    
    QString maskedCardNumber() const { return m_maskedCardNumber; }
    void setMaskedCardNumber(const QString& n) { m_maskedCardNumber = n; }
    
    QString cardholderName() const { return m_cardholderName; }
    void setCardholderName(const QString& n) { m_cardholderName = n; }
    
    QString expirationDate() const { return m_expirationDate; }
    void setExpirationDate(const QString& d) { m_expirationDate = d; }
    
    EntryMethod entryMethod() const { return m_entryMethod; }
    void setEntryMethod(EntryMethod m) { m_entryMethod = m; }
    
    // Timestamps
    QDateTime requestedAt() const { return m_requestedAt; }
    void setRequestedAt(const QDateTime& dt) { m_requestedAt = dt; }
    
    QDateTime completedAt() const { return m_completedAt; }
    void setCompletedAt(const QDateTime& dt) { m_completedAt = dt; }
    
    // Response
    QString responseCode() const { return m_responseCode; }
    void setResponseCode(const QString& code) { m_responseCode = code; }
    
    QString responseMessage() const { return m_responseMessage; }
    void setResponseMessage(const QString& msg) { m_responseMessage = msg; }
    
    QString avsResult() const { return m_avsResult; }
    void setAvsResult(const QString& r) { m_avsResult = r; }
    
    QString cvvResult() const { return m_cvvResult; }
    void setCvvResult(const QString& r) { m_cvvResult = r; }
    
    // Associated data
    int checkId() const { return m_checkId; }
    void setCheckId(int id) { m_checkId = id; }
    
    int employeeId() const { return m_employeeId; }
    void setEmployeeId(int id) { m_employeeId = id; }
    
    int terminalId() const { return m_terminalId; }
    void setTerminalId(int id) { m_terminalId = id; }
    
    // Batch info
    int batchId() const { return m_batchId; }
    void setBatchId(int id) { m_batchId = id; }
    
    bool isSettled() const { return m_isSettled; }
    void setSettled(bool s) { m_isSettled = s; }
    
    // Signature
    QByteArray signatureData() const { return m_signatureData; }
    void setSignatureData(const QByteArray& sig) { m_signatureData = sig; }
    
    // Receipt data
    QString receiptText() const { return m_receiptText; }
    void setReceiptText(const QString& r) { m_receiptText = r; }
    
    QJsonObject toJson() const;
    static CreditCardTransaction* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    int m_id = 0;
    QString m_referenceNumber;
    QString m_authCode;
    QString m_processorTransactionId;
    
    TransactionType m_transactionType = TransactionType::Sale;
    TransactionStatus m_status = TransactionStatus::Pending;
    int m_amount = 0;
    int m_tipAmount = 0;
    
    CardType m_cardType = CardType::Unknown;
    QString m_maskedCardNumber;
    QString m_cardholderName;
    QString m_expirationDate;
    EntryMethod m_entryMethod = EntryMethod::Swipe;
    
    QDateTime m_requestedAt;
    QDateTime m_completedAt;
    
    QString m_responseCode;
    QString m_responseMessage;
    QString m_avsResult;
    QString m_cvvResult;
    
    int m_checkId = 0;
    int m_employeeId = 0;
    int m_terminalId = 0;
    
    int m_batchId = 0;
    bool m_isSettled = false;
    
    QByteArray m_signatureData;
    QString m_receiptText;
};

//=============================================================================
// Batch - Collection of transactions for settlement
//=============================================================================
class CardBatch : public QObject {
    Q_OBJECT
    
public:
    enum class BatchStatus {
        Open,
        Closing,
        Closed,
        Failed
    };
    
    explicit CardBatch(QObject* parent = nullptr);
    
    int id() const { return m_id; }
    void setId(int id) { m_id = id; }
    
    QString batchNumber() const { return m_batchNumber; }
    void setBatchNumber(const QString& n) { m_batchNumber = n; }
    
    BatchStatus status() const { return m_status; }
    void setStatus(BatchStatus s) { m_status = s; }
    
    QDateTime openedAt() const { return m_openedAt; }
    void setOpenedAt(const QDateTime& dt) { m_openedAt = dt; }
    
    QDateTime closedAt() const { return m_closedAt; }
    void setClosedAt(const QDateTime& dt) { m_closedAt = dt; }
    
    int transactionCount() const { return m_transactionCount; }
    void setTransactionCount(int c) { m_transactionCount = c; }
    
    int totalAmount() const { return m_totalAmount; }
    void setTotalAmount(int cents) { m_totalAmount = cents; }
    
    int creditCount() const { return m_creditCount; }
    void setCreditCount(int c) { m_creditCount = c; }
    
    int creditAmount() const { return m_creditAmount; }
    void setCreditAmount(int cents) { m_creditAmount = cents; }
    
    QString closeResponse() const { return m_closeResponse; }
    void setCloseResponse(const QString& r) { m_closeResponse = r; }
    
    QJsonObject toJson() const;
    static CardBatch* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    int m_id = 0;
    QString m_batchNumber;
    BatchStatus m_status = BatchStatus::Open;
    QDateTime m_openedAt;
    QDateTime m_closedAt;
    int m_transactionCount = 0;
    int m_totalAmount = 0;
    int m_creditCount = 0;
    int m_creditAmount = 0;
    QString m_closeResponse;
};

//=============================================================================
// Processor Configuration
//=============================================================================
class ProcessorConfig : public QObject {
    Q_OBJECT
    
public:
    enum class ProcessorType {
        None,
        Mercury,       // Original ViewTouch integration
        Heartland,
        FirstData,
        Worldpay,
        Square,
        Stripe,
        PayPal,
        Custom
    };
    
    explicit ProcessorConfig(QObject* parent = nullptr);
    
    ProcessorType processorType() const { return m_processorType; }
    void setProcessorType(ProcessorType t) { m_processorType = t; }
    
    QString merchantId() const { return m_merchantId; }
    void setMerchantId(const QString& id) { m_merchantId = id; }
    
    QString terminalId() const { return m_terminalId; }
    void setTerminalId(const QString& id) { m_terminalId = id; }
    
    QString apiKey() const { return m_apiKey; }
    void setApiKey(const QString& key) { m_apiKey = key; }
    
    QString apiSecret() const { return m_apiSecret; }
    void setApiSecret(const QString& secret) { m_apiSecret = secret; }
    
    QString gatewayUrl() const { return m_gatewayUrl; }
    void setGatewayUrl(const QString& url) { m_gatewayUrl = url; }
    
    bool isTestMode() const { return m_testMode; }
    void setTestMode(bool t) { m_testMode = t; }
    
    int timeout() const { return m_timeout; }
    void setTimeout(int ms) { m_timeout = ms; }
    
    // Features
    bool supportsEMV() const { return m_supportsEMV; }
    void setSupportsEMV(bool s) { m_supportsEMV = s; }
    
    bool supportsContactless() const { return m_supportsContactless; }
    void setSupportsContactless(bool s) { m_supportsContactless = s; }
    
    bool requiresSignature() const { return m_requiresSignature; }
    void setRequiresSignature(bool r) { m_requiresSignature = r; }
    
    int signatureThreshold() const { return m_signatureThreshold; }
    void setSignatureThreshold(int cents) { m_signatureThreshold = cents; }
    
    bool autoSettleEnabled() const { return m_autoSettleEnabled; }
    void setAutoSettleEnabled(bool e) { m_autoSettleEnabled = e; }
    
    QString autoSettleTime() const { return m_autoSettleTime; }
    void setAutoSettleTime(const QString& t) { m_autoSettleTime = t; }
    
    QJsonObject toJson() const;
    static ProcessorConfig* fromJson(const QJsonObject& json, QObject* parent = nullptr);
    
private:
    ProcessorType m_processorType = ProcessorType::None;
    QString m_merchantId;
    QString m_terminalId;
    QString m_apiKey;
    QString m_apiSecret;
    QString m_gatewayUrl;
    bool m_testMode = true;
    int m_timeout = 30000;  // 30 seconds
    
    bool m_supportsEMV = true;
    bool m_supportsContactless = true;
    bool m_requiresSignature = true;
    int m_signatureThreshold = 2500;  // $25.00
    bool m_autoSettleEnabled = false;
    QString m_autoSettleTime = "23:00";
};

//=============================================================================
// Credit Card Manager - Singleton
//=============================================================================
class CreditCardManager : public QObject {
    Q_OBJECT
    
public:
    static CreditCardManager* instance();
    
    // Configuration
    ProcessorConfig* config() { return m_config; }
    void setConfig(ProcessorConfig* config);
    bool isConfigured() const;
    
    // Transaction processing
    CreditCardTransaction* processSale(int amount, int checkId = 0);
    CreditCardTransaction* processAuthOnly(int amount, int checkId = 0);
    CreditCardTransaction* captureAuth(CreditCardTransaction* auth, int amount = 0);
    CreditCardTransaction* processRefund(CreditCardTransaction* original, int amount = 0);
    CreditCardTransaction* voidTransaction(CreditCardTransaction* transaction);
    CreditCardTransaction* adjustTip(CreditCardTransaction* transaction, int tipAmount);
    
    // Manual entry
    CreditCardTransaction* manualSale(const QString& cardNumber, const QString& expDate,
                                      const QString& cvv, int amount, int checkId = 0);
    
    // Transaction lookup
    CreditCardTransaction* findTransaction(int id);
    CreditCardTransaction* findByReference(const QString& ref);
    QList<CreditCardTransaction*> transactionsForCheck(int checkId);
    QList<CreditCardTransaction*> transactionsForDate(const QDate& date);
    QList<CreditCardTransaction*> unsettledTransactions();
    
    // Batch operations
    CardBatch* currentBatch();
    CardBatch* openNewBatch();
    bool closeBatch();
    CardBatch* findBatch(int id);
    QList<CardBatch*> allBatches();
    
    // Card utilities
    static CardType detectCardType(const QString& cardNumber);
    static QString maskCardNumber(const QString& cardNumber);
    static bool validateCardNumber(const QString& cardNumber);  // Luhn check
    
    // Reports
    int totalSalesForDate(const QDate& date);
    int totalRefundsForDate(const QDate& date);
    QMap<CardType, int> salesByCardType(const QDate& date);
    
    // Persistence
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);
    
signals:
    void transactionStarted(CreditCardTransaction* transaction);
    void transactionCompleted(CreditCardTransaction* transaction);
    void transactionFailed(CreditCardTransaction* transaction, const QString& error);
    void batchOpened(CardBatch* batch);
    void batchClosed(CardBatch* batch);
    void signatureRequired(CreditCardTransaction* transaction);
    
private:
    explicit CreditCardManager(QObject* parent = nullptr);
    static CreditCardManager* s_instance;
    
    CreditCardTransaction* createTransaction(TransactionType type, int amount);
    bool sendToProcessor(CreditCardTransaction* transaction);
    void simulateResponse(CreditCardTransaction* transaction);
    
    ProcessorConfig* m_config;
    QList<CreditCardTransaction*> m_transactions;
    QList<CardBatch*> m_batches;
    CardBatch* m_currentBatch = nullptr;
    
    int m_nextTransactionId = 1;
    int m_nextBatchId = 1;
};

} // namespace vt2
