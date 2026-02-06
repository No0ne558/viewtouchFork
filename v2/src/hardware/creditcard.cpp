// ViewTouch V2 - Credit Card Processing Implementation
// Modern C++/Qt6 reimplementation

#include "creditcard.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QRandomGenerator>

namespace vt2 {

//=============================================================================
// CreditCardTransaction Implementation
//=============================================================================

CreditCardTransaction::CreditCardTransaction(QObject* parent)
    : QObject(parent)
    , m_requestedAt(QDateTime::currentDateTime())
{
}

QJsonObject CreditCardTransaction::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["referenceNumber"] = m_referenceNumber;
    json["authCode"] = m_authCode;
    json["processorTransactionId"] = m_processorTransactionId;
    json["transactionType"] = static_cast<int>(m_transactionType);
    json["status"] = static_cast<int>(m_status);
    json["amount"] = m_amount;
    json["tipAmount"] = m_tipAmount;
    json["cardType"] = static_cast<int>(m_cardType);
    json["maskedCardNumber"] = m_maskedCardNumber;
    json["cardholderName"] = m_cardholderName;
    json["expirationDate"] = m_expirationDate;
    json["entryMethod"] = static_cast<int>(m_entryMethod);
    json["requestedAt"] = m_requestedAt.toString(Qt::ISODate);
    if (m_completedAt.isValid()) {
        json["completedAt"] = m_completedAt.toString(Qt::ISODate);
    }
    json["responseCode"] = m_responseCode;
    json["responseMessage"] = m_responseMessage;
    json["avsResult"] = m_avsResult;
    json["cvvResult"] = m_cvvResult;
    json["checkId"] = m_checkId;
    json["employeeId"] = m_employeeId;
    json["terminalId"] = m_terminalId;
    json["batchId"] = m_batchId;
    json["isSettled"] = m_isSettled;
    json["signatureData"] = QString::fromLatin1(m_signatureData.toBase64());
    json["receiptText"] = m_receiptText;
    return json;
}

CreditCardTransaction* CreditCardTransaction::fromJson(const QJsonObject& json, QObject* parent) {
    auto* txn = new CreditCardTransaction(parent);
    txn->m_id = json["id"].toInt();
    txn->m_referenceNumber = json["referenceNumber"].toString();
    txn->m_authCode = json["authCode"].toString();
    txn->m_processorTransactionId = json["processorTransactionId"].toString();
    txn->m_transactionType = static_cast<TransactionType>(json["transactionType"].toInt());
    txn->m_status = static_cast<TransactionStatus>(json["status"].toInt());
    txn->m_amount = json["amount"].toInt();
    txn->m_tipAmount = json["tipAmount"].toInt();
    txn->m_cardType = static_cast<CardType>(json["cardType"].toInt());
    txn->m_maskedCardNumber = json["maskedCardNumber"].toString();
    txn->m_cardholderName = json["cardholderName"].toString();
    txn->m_expirationDate = json["expirationDate"].toString();
    txn->m_entryMethod = static_cast<EntryMethod>(json["entryMethod"].toInt());
    txn->m_requestedAt = QDateTime::fromString(json["requestedAt"].toString(), Qt::ISODate);
    if (json.contains("completedAt")) {
        txn->m_completedAt = QDateTime::fromString(json["completedAt"].toString(), Qt::ISODate);
    }
    txn->m_responseCode = json["responseCode"].toString();
    txn->m_responseMessage = json["responseMessage"].toString();
    txn->m_avsResult = json["avsResult"].toString();
    txn->m_cvvResult = json["cvvResult"].toString();
    txn->m_checkId = json["checkId"].toInt();
    txn->m_employeeId = json["employeeId"].toInt();
    txn->m_terminalId = json["terminalId"].toInt();
    txn->m_batchId = json["batchId"].toInt();
    txn->m_isSettled = json["isSettled"].toBool();
    txn->m_signatureData = QByteArray::fromBase64(json["signatureData"].toString().toLatin1());
    txn->m_receiptText = json["receiptText"].toString();
    return txn;
}

//=============================================================================
// CardBatch Implementation
//=============================================================================

CardBatch::CardBatch(QObject* parent)
    : QObject(parent)
    , m_openedAt(QDateTime::currentDateTime())
{
}

QJsonObject CardBatch::toJson() const {
    QJsonObject json;
    json["id"] = m_id;
    json["batchNumber"] = m_batchNumber;
    json["status"] = static_cast<int>(m_status);
    json["openedAt"] = m_openedAt.toString(Qt::ISODate);
    if (m_closedAt.isValid()) {
        json["closedAt"] = m_closedAt.toString(Qt::ISODate);
    }
    json["transactionCount"] = m_transactionCount;
    json["totalAmount"] = m_totalAmount;
    json["creditCount"] = m_creditCount;
    json["creditAmount"] = m_creditAmount;
    json["closeResponse"] = m_closeResponse;
    return json;
}

CardBatch* CardBatch::fromJson(const QJsonObject& json, QObject* parent) {
    auto* batch = new CardBatch(parent);
    batch->m_id = json["id"].toInt();
    batch->m_batchNumber = json["batchNumber"].toString();
    batch->m_status = static_cast<BatchStatus>(json["status"].toInt());
    batch->m_openedAt = QDateTime::fromString(json["openedAt"].toString(), Qt::ISODate);
    if (json.contains("closedAt")) {
        batch->m_closedAt = QDateTime::fromString(json["closedAt"].toString(), Qt::ISODate);
    }
    batch->m_transactionCount = json["transactionCount"].toInt();
    batch->m_totalAmount = json["totalAmount"].toInt();
    batch->m_creditCount = json["creditCount"].toInt();
    batch->m_creditAmount = json["creditAmount"].toInt();
    batch->m_closeResponse = json["closeResponse"].toString();
    return batch;
}

//=============================================================================
// ProcessorConfig Implementation
//=============================================================================

ProcessorConfig::ProcessorConfig(QObject* parent)
    : QObject(parent)
{
}

QJsonObject ProcessorConfig::toJson() const {
    QJsonObject json;
    json["processorType"] = static_cast<int>(m_processorType);
    json["merchantId"] = m_merchantId;
    json["terminalId"] = m_terminalId;
    // Note: In production, encrypt these before saving
    json["apiKey"] = m_apiKey;
    json["apiSecret"] = m_apiSecret;
    json["gatewayUrl"] = m_gatewayUrl;
    json["testMode"] = m_testMode;
    json["timeout"] = m_timeout;
    json["supportsEMV"] = m_supportsEMV;
    json["supportsContactless"] = m_supportsContactless;
    json["requiresSignature"] = m_requiresSignature;
    json["signatureThreshold"] = m_signatureThreshold;
    json["autoSettleEnabled"] = m_autoSettleEnabled;
    json["autoSettleTime"] = m_autoSettleTime;
    return json;
}

ProcessorConfig* ProcessorConfig::fromJson(const QJsonObject& json, QObject* parent) {
    auto* config = new ProcessorConfig(parent);
    config->m_processorType = static_cast<ProcessorType>(json["processorType"].toInt());
    config->m_merchantId = json["merchantId"].toString();
    config->m_terminalId = json["terminalId"].toString();
    config->m_apiKey = json["apiKey"].toString();
    config->m_apiSecret = json["apiSecret"].toString();
    config->m_gatewayUrl = json["gatewayUrl"].toString();
    config->m_testMode = json["testMode"].toBool(true);
    config->m_timeout = json["timeout"].toInt(30000);
    config->m_supportsEMV = json["supportsEMV"].toBool(true);
    config->m_supportsContactless = json["supportsContactless"].toBool(true);
    config->m_requiresSignature = json["requiresSignature"].toBool(true);
    config->m_signatureThreshold = json["signatureThreshold"].toInt(2500);
    config->m_autoSettleEnabled = json["autoSettleEnabled"].toBool();
    config->m_autoSettleTime = json["autoSettleTime"].toString("23:00");
    return config;
}

//=============================================================================
// CreditCardManager Implementation
//=============================================================================

CreditCardManager* CreditCardManager::s_instance = nullptr;

CreditCardManager::CreditCardManager(QObject* parent)
    : QObject(parent)
    , m_config(new ProcessorConfig(this))
{
}

CreditCardManager* CreditCardManager::instance() {
    if (!s_instance) {
        s_instance = new CreditCardManager();
    }
    return s_instance;
}

void CreditCardManager::setConfig(ProcessorConfig* config) {
    if (m_config && m_config != config) {
        delete m_config;
    }
    m_config = config;
    if (m_config) {
        m_config->setParent(this);
    }
}

bool CreditCardManager::isConfigured() const {
    return m_config && 
           m_config->processorType() != ProcessorConfig::ProcessorType::None &&
           !m_config->merchantId().isEmpty();
}

CreditCardTransaction* CreditCardManager::createTransaction(TransactionType type, int amount) {
    auto* txn = new CreditCardTransaction(this);
    txn->setId(m_nextTransactionId++);
    txn->setTransactionType(type);
    txn->setAmount(amount);
    txn->setStatus(TransactionStatus::Pending);
    
    // Generate reference number
    QString ref = QString("REF%1%2")
        .arg(QDateTime::currentDateTime().toString("yyyyMMddHHmmss"))
        .arg(txn->id(), 4, 10, QChar('0'));
    txn->setReferenceNumber(ref);
    
    // Assign to current batch
    if (m_currentBatch) {
        txn->setBatchId(m_currentBatch->id());
    }
    
    m_transactions.append(txn);
    return txn;
}

CreditCardTransaction* CreditCardManager::processSale(int amount, int checkId) {
    auto* txn = createTransaction(TransactionType::Sale, amount);
    txn->setCheckId(checkId);
    
    emit transactionStarted(txn);
    
    if (sendToProcessor(txn)) {
        emit transactionCompleted(txn);
    } else {
        emit transactionFailed(txn, txn->responseMessage());
    }
    
    return txn;
}

CreditCardTransaction* CreditCardManager::processAuthOnly(int amount, int checkId) {
    auto* txn = createTransaction(TransactionType::AuthOnly, amount);
    txn->setCheckId(checkId);
    
    emit transactionStarted(txn);
    
    if (sendToProcessor(txn)) {
        emit transactionCompleted(txn);
    } else {
        emit transactionFailed(txn, txn->responseMessage());
    }
    
    return txn;
}

CreditCardTransaction* CreditCardManager::captureAuth(CreditCardTransaction* auth, int amount) {
    if (!auth || auth->transactionType() != TransactionType::AuthOnly) {
        return nullptr;
    }
    
    int captureAmount = (amount > 0) ? amount : auth->amount();
    auto* txn = createTransaction(TransactionType::Capture, captureAmount);
    txn->setCheckId(auth->checkId());
    txn->setProcessorTransactionId(auth->processorTransactionId());
    
    emit transactionStarted(txn);
    
    if (sendToProcessor(txn)) {
        emit transactionCompleted(txn);
    } else {
        emit transactionFailed(txn, txn->responseMessage());
    }
    
    return txn;
}

CreditCardTransaction* CreditCardManager::processRefund(CreditCardTransaction* original, int amount) {
    int refundAmount = (amount > 0) ? amount : original->totalAmount();
    auto* txn = createTransaction(TransactionType::Refund, refundAmount);
    txn->setCheckId(original->checkId());
    txn->setProcessorTransactionId(original->processorTransactionId());
    txn->setMaskedCardNumber(original->maskedCardNumber());
    txn->setCardType(original->cardType());
    
    emit transactionStarted(txn);
    
    if (sendToProcessor(txn)) {
        emit transactionCompleted(txn);
    } else {
        emit transactionFailed(txn, txn->responseMessage());
    }
    
    return txn;
}

CreditCardTransaction* CreditCardManager::voidTransaction(CreditCardTransaction* transaction) {
    if (!transaction || transaction->isSettled()) {
        return nullptr;  // Can't void settled transactions
    }
    
    auto* txn = createTransaction(TransactionType::Void, transaction->totalAmount());
    txn->setCheckId(transaction->checkId());
    txn->setProcessorTransactionId(transaction->processorTransactionId());
    
    emit transactionStarted(txn);
    
    if (sendToProcessor(txn)) {
        transaction->setStatus(TransactionStatus::Voided);
        emit transactionCompleted(txn);
    } else {
        emit transactionFailed(txn, txn->responseMessage());
    }
    
    return txn;
}

CreditCardTransaction* CreditCardManager::adjustTip(CreditCardTransaction* transaction, int tipAmount) {
    if (!transaction || transaction->isSettled()) {
        return nullptr;
    }
    
    auto* txn = createTransaction(TransactionType::Adjustment, transaction->amount());
    txn->setTipAmount(tipAmount);
    txn->setCheckId(transaction->checkId());
    txn->setProcessorTransactionId(transaction->processorTransactionId());
    
    emit transactionStarted(txn);
    
    if (sendToProcessor(txn)) {
        transaction->setTipAmount(tipAmount);
        emit transactionCompleted(txn);
    } else {
        emit transactionFailed(txn, txn->responseMessage());
    }
    
    return txn;
}

CreditCardTransaction* CreditCardManager::manualSale(const QString& cardNumber, const QString& expDate,
                                                     const QString& cvv, int amount, int checkId) {
    Q_UNUSED(cvv);  // Used in actual processor communication
    
    auto* txn = createTransaction(TransactionType::Sale, amount);
    txn->setCheckId(checkId);
    txn->setEntryMethod(EntryMethod::Manual);
    txn->setMaskedCardNumber(maskCardNumber(cardNumber));
    txn->setCardType(detectCardType(cardNumber));
    txn->setExpirationDate(expDate);
    
    emit transactionStarted(txn);
    
    if (sendToProcessor(txn)) {
        emit transactionCompleted(txn);
    } else {
        emit transactionFailed(txn, txn->responseMessage());
    }
    
    return txn;
}

CreditCardTransaction* CreditCardManager::findTransaction(int id) {
    for (auto* txn : m_transactions) {
        if (txn->id() == id) return txn;
    }
    return nullptr;
}

CreditCardTransaction* CreditCardManager::findByReference(const QString& ref) {
    for (auto* txn : m_transactions) {
        if (txn->referenceNumber() == ref) return txn;
    }
    return nullptr;
}

QList<CreditCardTransaction*> CreditCardManager::transactionsForCheck(int checkId) {
    QList<CreditCardTransaction*> result;
    for (auto* txn : m_transactions) {
        if (txn->checkId() == checkId) {
            result.append(txn);
        }
    }
    return result;
}

QList<CreditCardTransaction*> CreditCardManager::transactionsForDate(const QDate& date) {
    QList<CreditCardTransaction*> result;
    for (auto* txn : m_transactions) {
        if (txn->requestedAt().date() == date) {
            result.append(txn);
        }
    }
    return result;
}

QList<CreditCardTransaction*> CreditCardManager::unsettledTransactions() {
    QList<CreditCardTransaction*> result;
    for (auto* txn : m_transactions) {
        if (!txn->isSettled() && 
            (txn->status() == TransactionStatus::Approved ||
             txn->transactionType() == TransactionType::AuthOnly)) {
            result.append(txn);
        }
    }
    return result;
}

CardBatch* CreditCardManager::currentBatch() {
    return m_currentBatch;
}

CardBatch* CreditCardManager::openNewBatch() {
    if (m_currentBatch && m_currentBatch->status() == CardBatch::BatchStatus::Open) {
        closeBatch();
    }
    
    m_currentBatch = new CardBatch(this);
    m_currentBatch->setId(m_nextBatchId++);
    m_currentBatch->setBatchNumber(QString("B%1").arg(m_currentBatch->id(), 6, 10, QChar('0')));
    m_currentBatch->setStatus(CardBatch::BatchStatus::Open);
    
    m_batches.append(m_currentBatch);
    
    emit batchOpened(m_currentBatch);
    return m_currentBatch;
}

bool CreditCardManager::closeBatch() {
    if (!m_currentBatch || m_currentBatch->status() != CardBatch::BatchStatus::Open) {
        return false;
    }
    
    m_currentBatch->setStatus(CardBatch::BatchStatus::Closing);
    
    // Calculate batch totals
    int count = 0;
    int total = 0;
    int creditCount = 0;
    int creditTotal = 0;
    
    for (auto* txn : m_transactions) {
        if (txn->batchId() == m_currentBatch->id() && 
            txn->status() == TransactionStatus::Approved) {
            
            if (txn->transactionType() == TransactionType::Sale ||
                txn->transactionType() == TransactionType::Capture) {
                count++;
                total += txn->totalAmount();
            } else if (txn->transactionType() == TransactionType::Refund) {
                creditCount++;
                creditTotal += txn->amount();
            }
            
            txn->setSettled(true);
        }
    }
    
    m_currentBatch->setTransactionCount(count);
    m_currentBatch->setTotalAmount(total);
    m_currentBatch->setCreditCount(creditCount);
    m_currentBatch->setCreditAmount(creditTotal);
    m_currentBatch->setClosedAt(QDateTime::currentDateTime());
    m_currentBatch->setStatus(CardBatch::BatchStatus::Closed);
    m_currentBatch->setCloseResponse("Batch closed successfully");
    
    emit batchClosed(m_currentBatch);
    
    // Open new batch automatically
    openNewBatch();
    
    return true;
}

CardBatch* CreditCardManager::findBatch(int id) {
    for (auto* batch : m_batches) {
        if (batch->id() == id) return batch;
    }
    return nullptr;
}

QList<CardBatch*> CreditCardManager::allBatches() {
    return m_batches;
}

CardType CreditCardManager::detectCardType(const QString& cardNumber) {
    QString num = cardNumber;
    num.remove(' ').remove('-');
    
    if (num.isEmpty()) return CardType::Unknown;
    
    // Visa: starts with 4
    if (num.startsWith('4')) return CardType::Visa;
    
    // Mastercard: starts with 51-55 or 2221-2720
    if (num.length() >= 2) {
        int prefix2 = num.left(2).toInt();
        if (prefix2 >= 51 && prefix2 <= 55) return CardType::MasterCard;
        if (num.length() >= 4) {
            int prefix4 = num.left(4).toInt();
            if (prefix4 >= 2221 && prefix4 <= 2720) return CardType::MasterCard;
        }
    }
    
    // Amex: starts with 34 or 37
    if (num.startsWith("34") || num.startsWith("37")) return CardType::Amex;
    
    // Discover: starts with 6011, 622126-622925, 644-649, 65
    if (num.startsWith("6011") || num.startsWith("65")) return CardType::Discover;
    if (num.length() >= 6) {
        int prefix6 = num.left(6).toInt();
        if (prefix6 >= 622126 && prefix6 <= 622925) return CardType::Discover;
    }
    if (num.length() >= 3) {
        int prefix3 = num.left(3).toInt();
        if (prefix3 >= 644 && prefix3 <= 649) return CardType::Discover;
    }
    
    // Diners Club: starts with 300-305, 36, 38
    if (num.startsWith("36") || num.startsWith("38")) return CardType::DinersClub;
    if (num.length() >= 3) {
        int prefix3 = num.left(3).toInt();
        if (prefix3 >= 300 && prefix3 <= 305) return CardType::DinersClub;
    }
    
    // JCB: starts with 35
    if (num.startsWith("35")) return CardType::JCB;
    
    return CardType::Unknown;
}

QString CreditCardManager::maskCardNumber(const QString& cardNumber) {
    QString num = cardNumber;
    num.remove(' ').remove('-');
    
    if (num.length() < 4) return "****";
    
    // Show last 4 digits
    QString last4 = num.right(4);
    int maskLen = num.length() - 4;
    
    return QString(maskLen, '*') + last4;
}

bool CreditCardManager::validateCardNumber(const QString& cardNumber) {
    QString num = cardNumber;
    num.remove(' ').remove('-');
    
    if (num.isEmpty() || num.length() < 13 || num.length() > 19) {
        return false;
    }
    
    // Luhn algorithm
    int sum = 0;
    bool alternate = false;
    
    for (int i = num.length() - 1; i >= 0; --i) {
        int n = num[i].digitValue();
        if (n < 0) return false;  // Not a digit
        
        if (alternate) {
            n *= 2;
            if (n > 9) n -= 9;
        }
        
        sum += n;
        alternate = !alternate;
    }
    
    return (sum % 10 == 0);
}

int CreditCardManager::totalSalesForDate(const QDate& date) {
    int total = 0;
    for (const auto* txn : m_transactions) {
        if (txn->requestedAt().date() == date &&
            txn->status() == TransactionStatus::Approved &&
            (txn->transactionType() == TransactionType::Sale ||
             txn->transactionType() == TransactionType::Capture)) {
            total += txn->totalAmount();
        }
    }
    return total;
}

int CreditCardManager::totalRefundsForDate(const QDate& date) {
    int total = 0;
    for (const auto* txn : m_transactions) {
        if (txn->requestedAt().date() == date &&
            txn->status() == TransactionStatus::Approved &&
            txn->transactionType() == TransactionType::Refund) {
            total += txn->amount();
        }
    }
    return total;
}

QMap<CardType, int> CreditCardManager::salesByCardType(const QDate& date) {
    QMap<CardType, int> result;
    for (const auto* txn : m_transactions) {
        if (txn->requestedAt().date() == date &&
            txn->status() == TransactionStatus::Approved &&
            (txn->transactionType() == TransactionType::Sale ||
             txn->transactionType() == TransactionType::Capture)) {
            result[txn->cardType()] += txn->totalAmount();
        }
    }
    return result;
}

bool CreditCardManager::sendToProcessor(CreditCardTransaction* transaction) {
    // In test mode or if no processor configured, simulate response
    if (!isConfigured() || m_config->isTestMode()) {
        simulateResponse(transaction);
        return transaction->status() == TransactionStatus::Approved;
    }
    
    // Real processor communication would go here
    // For now, simulate
    simulateResponse(transaction);
    return transaction->status() == TransactionStatus::Approved;
}

void CreditCardManager::simulateResponse(CreditCardTransaction* transaction) {
    // Simulate processing delay would be here in real implementation
    
    transaction->setCompletedAt(QDateTime::currentDateTime());
    
    // Simulate card data if not set
    if (transaction->maskedCardNumber().isEmpty()) {
        transaction->setMaskedCardNumber("************1234");
        transaction->setCardType(CardType::Visa);
        transaction->setExpirationDate("12/25");
        transaction->setCardholderName("TEST CARDHOLDER");
        transaction->setEntryMethod(EntryMethod::Chip);
    }
    
    // Simulate approval (95% success rate in test mode)
    int roll = QRandomGenerator::global()->bounded(100);
    if (roll < 95) {
        transaction->setStatus(TransactionStatus::Approved);
        transaction->setResponseCode("00");
        transaction->setResponseMessage("APPROVED");
        
        // Generate auth code
        QString auth = QString("%1").arg(QRandomGenerator::global()->bounded(999999), 6, 10, QChar('0'));
        transaction->setAuthCode(auth);
        
        transaction->setAvsResult("Y");  // Address match
        transaction->setCvvResult("M");  // CVV match
        
    } else if (roll < 98) {
        transaction->setStatus(TransactionStatus::Declined);
        transaction->setResponseCode("05");
        transaction->setResponseMessage("DO NOT HONOR");
    } else {
        transaction->setStatus(TransactionStatus::Error);
        transaction->setResponseCode("96");
        transaction->setResponseMessage("SYSTEM ERROR");
    }
    
    // Generate receipt text
    QString receipt = QString(
        "================================\n"
        "        MERCHANT COPY\n"
        "================================\n"
        "%1\n"
        "Card: %2\n"
        "Entry: %3\n"
        "\n"
        "Amount: $%4.%5\n"
        "Tip:    $%6.%7\n"
        "Total:  $%8.%9\n"
        "\n"
        "Auth: %10\n"
        "Ref:  %11\n"
        "\n"
        "%12\n"
        "================================\n"
    ).arg(transaction->cardholderName())
     .arg(transaction->maskedCardNumber())
     .arg(transaction->entryMethod() == EntryMethod::Chip ? "CHIP" : "SWIPE")
     .arg(transaction->amount() / 100)
     .arg(transaction->amount() % 100, 2, 10, QChar('0'))
     .arg(transaction->tipAmount() / 100)
     .arg(transaction->tipAmount() % 100, 2, 10, QChar('0'))
     .arg(transaction->totalAmount() / 100)
     .arg(transaction->totalAmount() % 100, 2, 10, QChar('0'))
     .arg(transaction->authCode())
     .arg(transaction->referenceNumber())
     .arg(transaction->responseMessage());
    
    transaction->setReceiptText(receipt);
}

bool CreditCardManager::saveToFile(const QString& path) {
    QJsonObject root;
    root["nextTransactionId"] = m_nextTransactionId;
    root["nextBatchId"] = m_nextBatchId;
    
    if (m_config) {
        root["config"] = m_config->toJson();
    }
    
    if (m_currentBatch) {
        root["currentBatchId"] = m_currentBatch->id();
    }
    
    QJsonArray txnArray;
    for (const auto* txn : m_transactions) {
        txnArray.append(txn->toJson());
    }
    root["transactions"] = txnArray;
    
    QJsonArray batchArray;
    for (const auto* batch : m_batches) {
        batchArray.append(batch->toJson());
    }
    root["batches"] = batchArray;
    
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonDocument doc(root);
    file.write(doc.toJson());
    return true;
}

bool CreditCardManager::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    
    m_nextTransactionId = root["nextTransactionId"].toInt(1);
    m_nextBatchId = root["nextBatchId"].toInt(1);
    
    if (root.contains("config")) {
        delete m_config;
        m_config = ProcessorConfig::fromJson(root["config"].toObject(), this);
    }
    
    qDeleteAll(m_transactions);
    m_transactions.clear();
    
    QJsonArray txnArray = root["transactions"].toArray();
    for (const auto& ref : txnArray) {
        auto* txn = CreditCardTransaction::fromJson(ref.toObject(), this);
        m_transactions.append(txn);
    }
    
    qDeleteAll(m_batches);
    m_batches.clear();
    m_currentBatch = nullptr;
    
    QJsonArray batchArray = root["batches"].toArray();
    for (const auto& ref : batchArray) {
        auto* batch = CardBatch::fromJson(ref.toObject(), this);
        m_batches.append(batch);
    }
    
    int currentBatchId = root["currentBatchId"].toInt();
    if (currentBatchId > 0) {
        m_currentBatch = findBatch(currentBatchId);
    }
    
    return true;
}

} // namespace vt2
