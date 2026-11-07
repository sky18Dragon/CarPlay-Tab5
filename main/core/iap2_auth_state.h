#pragma once
namespace iap2 {
/* Transport ACK is required before accepting peer authentication success.
 * Failure is terminal until a new per-connection object is constructed. */
class AuthState {
public:
    enum class Stage { idle, certificate, signature, acknowledged, accepted, failed };
private:
    Stage stage_=Stage::idle;
    bool move(Stage from,Stage to) {
        if(stage_!=from) { stage_=Stage::failed; return false; }
        stage_=to; return true;
    }
public:
    bool certificate_sent() { return move(Stage::idle,Stage::certificate); }
    bool can_sign() const { return stage_==Stage::certificate; }
    bool signature_sent() { return move(Stage::certificate,Stage::signature); }
    bool signature_acknowledged() { return move(Stage::signature,Stage::acknowledged); }
    bool accept() { return move(Stage::acknowledged,Stage::accepted); }
    void reject() { stage_=Stage::failed; }
    bool accepted() const { return stage_==Stage::accepted; }
};
}
