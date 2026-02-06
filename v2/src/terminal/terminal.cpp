/*
 * ViewTouch V2 - Terminal Implementation
 */

#include "terminal/terminal.hpp"
#include "zone/zone.hpp"
#include "zone/page.hpp"
#include "zone/zone_db.hpp"
#include "render/renderer.hpp"

namespace vt {

Terminal::Terminal(QObject* parent)
    : QObject(parent)
{
    pageStack_.fill(nullptr);
}

Terminal::~Terminal() = default;

bool Terminal::jumpToPage(int pageId, JumpType jt) {
    if (!zoneDb_) return false;
    
    Page* page = zoneDb_->page(pageId);
    return jumpToPage(page, jt);
}

bool Terminal::jumpToPage(Page* page, JumpType jt) {
    if (!page) return false;
    
    Page* oldPage = currentPage_;
    
    switch (jt) {
        case JumpType::Normal:
            // Push current page onto stack
            if (currentPage_ && stackIndex_ < PAGE_STACK_SIZE - 1) {
                pageStack_[stackIndex_++] = currentPage_;
            }
            currentPage_ = page;
            break;
            
        case JumpType::Stealth:
            // Don't push to stack (replace)
            currentPage_ = page;
            break;
            
        case JumpType::Return:
            // Pop from stack
            if (stackIndex_ > 0) {
                currentPage_ = pageStack_[--stackIndex_];
            }
            break;
            
        case JumpType::Home:
            // Clear stack and go home
            clearPageStack();
            currentPage_ = page;
            break;
            
        case JumpType::Script:
            // Script jump - push to stack
            if (currentPage_ && stackIndex_ < PAGE_STACK_SIZE - 1) {
                pageStack_[stackIndex_++] = currentPage_;
            }
            currentPage_ = page;
            break;
            
        case JumpType::Index:
            // Index jump - push to stack
            if (currentPage_ && stackIndex_ < PAGE_STACK_SIZE - 1) {
                pageStack_[stackIndex_++] = currentPage_;
            }
            currentPage_ = page;
            break;
            
        case JumpType::Password:
            // Password protected - push to stack
            if (currentPage_ && stackIndex_ < PAGE_STACK_SIZE - 1) {
                pageStack_[stackIndex_++] = currentPage_;
            }
            currentPage_ = page;
            break;
    }
    
    if (currentPage_ != oldPage) {
        emit pageChanged(currentPage_, oldPage);
        requestRedraw();
    }
    
    return true;
}

bool Terminal::jumpBack() {
    if (stackIndex_ > 0) {
        Page* oldPage = currentPage_;
        currentPage_ = pageStack_[--stackIndex_];
        pageStack_[stackIndex_] = nullptr;
        emit pageChanged(currentPage_, oldPage);
        requestRedraw();
        return true;
    }
    return false;
}

bool Terminal::jumpHome() {
    if (zoneDb_ && homePageId_ > 0) {
        Page* homePage = zoneDb_->page(homePageId_);
        if (homePage) {
            clearPageStack();
            Page* oldPage = currentPage_;
            currentPage_ = homePage;
            emit pageChanged(currentPage_, oldPage);
            requestRedraw();
            return true;
        }
    }
    return false;
}

void Terminal::clearPageStack() {
    pageStack_.fill(nullptr);
    stackIndex_ = 0;
}

int Terminal::previousPageId() const {
    if (stackIndex_ > 0 && pageStack_[stackIndex_ - 1]) {
        return pageStack_[stackIndex_ - 1]->id();
    }
    return 0;
}

void Terminal::touch(int x, int y) {
    if (!currentPage_) return;
    
    Zone* zone = currentPage_->findZone(x, y);
    if (zone) {
        zone->touch(this, x, y);
    }
}

void Terminal::release(int x, int y) {
    if (selectedZone_) {
        selectedZone_->touchRelease(this, x, y);
    }
}

int Terminal::signal(const QString& message, int groupId) {
    // Process message signals from zones
    // Messages can be commands like "save", "cancel", "done", etc.
    
    if (message.isEmpty()) {
        return 0;
    }
    
    // Common messages
    if (message == QStringLiteral("done") || message == QStringLiteral("cancel")) {
        jumpBack();
        return 1;
    }
    
    if (message == QStringLiteral("home")) {
        jumpHome();
        return 1;
    }
    
    if (message == QStringLiteral("logout")) {
        setUserId(0);
        setUserName(QString());
        jumpHome();
        return 1;
    }
    
    // TODO: Implement more message handlers
    // "save", "print", "clear", etc.
    
    return 0;
}

void Terminal::setSelectedZone(Zone* zone) {
    if (selectedZone_ != zone) {
        if (selectedZone_) {
            selectedZone_->setSelected(false);
        }
        selectedZone_ = zone;
        if (selectedZone_) {
            selectedZone_->setSelected(true);
        }
        emit selectionChanged(selectedZone_);
        requestRedraw();
    }
}

void Terminal::clearSelection() {
    setSelectedZone(nullptr);
}

void Terminal::draw(Renderer& renderer) {
    if (currentPage_) {
        currentPage_->render(renderer, this);
    }
    needsRedraw_ = false;
}

void Terminal::update(UpdateFlag flags, const QString& value) {
    if (currentPage_) {
        currentPage_->update(this, flags, value);
        requestRedraw();
    }
}

void Terminal::requestRedraw() {
    needsRedraw_ = true;
    emit redrawRequested();
}

} // namespace vt
