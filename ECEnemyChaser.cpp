#include "ECEnemyChaser.h"
#include "ECFieldOfViewEmitter.h"
#include "ECPathMover.h"
#include <cassert>
#include <QTimer>
#include "Map.h"
#include "Utilities.h"
#include "Game.h"
#include <QDebug> // TODO: test remove

ECEnemyChaser::ECEnemyChaser(Entity* entity):
    EntityController(entity),
    stopDistance_(100),
    fovEmitter_(new ECFieldOfViewEmitter(entity)),
    pathMover_(new ECPathMover(entity)),
    chaseTimer_(new QTimer(this)),
    shouldChase_(true),
    paused_(false),
    targetEntity_(nullptr)
{
    // make sure entity is in a map
    Map* entitysMap = entity->map();
    assert(entitysMap != nullptr);

    // make sure entity's map is in a game
    Game* entitysGame = entitysMap->game();
    assert(entitysGame != nullptr);

    // listen to game
    connect(entitysGame, &Game::watchedEntityEntersRange, this, &ECEnemyChaser::onEntityEntersRange_);
    connect(entitysGame, &Game::watchedEntityLeavesRange, this, &ECEnemyChaser::onEntityLeavesRange_);

    // listen to fov emitter
    connect(fovEmitter_.get(),&ECFieldOfViewEmitter::entityEntersFOV,this,&ECEnemyChaser::onEntityEntersFOV_);
    connect(fovEmitter_.get(),&ECFieldOfViewEmitter::entityLeavesFOV,this,&ECEnemyChaser::onEntityLeavesFOV_);

    // listen to path mover
    connect(pathMover_.get(),&ECPathMover::moved,this,&ECEnemyChaser::onEntityMoved_);

    // listen to when the chasing entity dies
    connect(entityControlled(),&QObject::destroyed,this,&ECEnemyChaser::onChasingEntityDies_);

    // listen to when chasing entity leaves map
    connect(entityControlled(),&Entity::mapLeft,this, &ECEnemyChaser::onChasingEntityLeavesMap_);

    // connect timer
    connect(chaseTimer_,&QTimer::timeout,this,&ECEnemyChaser::chaseStep_);

    // set up path mover
    pathMover_->setAlwaysFaceTargetPosition(true);
    pathMover_->setEntity(entity);
}

/// Makes it so the controlled entity stops chasing enemy entities.
/// The controlled entity will then simply ignore any enemy entities that enter
/// its field of view.
/// If the controlled entity is currently chasing, it will stop.
void ECEnemyChaser::stopChasing()
{    
    // if currently chasing stop
    if (targetEntity_ != nullptr){
        targetEntity_ = nullptr;
        chaseTimer_->disconnect();
    }

    shouldChase_ = false;
}

/// Makes it so that the controlled entity will chase any enemy entites that
/// enter its field of view.
void ECEnemyChaser::startChasing()
{
    shouldChase_ = true;
}

/// Sets the stop distance.
/// See stopDistance() for more info.
void ECEnemyChaser::setStopDistance(double distance)
{
    stopDistance_ = distance;
}

/// Returns the distance that the controlled Entity will stop before the
/// positions of the chased entity.
double ECEnemyChaser::stopDistance()
{
    return stopDistance_;
}

/// Executed whenever an entity enters the fov of the controlled entity.
/// If the controlled entity doesn't already have a target entity, will set the newly
/// entering entity as the target entity.
void ECEnemyChaser::onEntityEntersFOV_(Entity *entity)
{
    // TODO: test remove
    qDebug() << "onEntityEntersFOF_ executed";

    // if the controlled entity isn't supposed to chase anything, do nothing
    if (!shouldChase_)
        return;

    // if the controlled entity already has a target entity, do nothing
    if (targetEntity_ != nullptr)
        return;

    // if the entering entity is not an enemy, do nothing
    if (entityControlled()->isAnEnemyGroup(entity->group()) == false)
        return;

    // listen to when target entity dies (so we can stop chasing)
    targetEntity_ = entity;
    disconnect(0,&QObject::destroyed,this,&ECEnemyChaser::onChasedEntityDies_); // disconnect any previous signals connect to this slot
    connect(entity,&QObject::destroyed,this,&ECEnemyChaser::onChasedEntityDies_);

    // listen to when target entity leaves map (so we can stop chasing)
    disconnect(0,&Entity::mapLeft,this,&ECEnemyChaser::onChasedEntityLeavesMap_);
    connect(entity,&Entity::mapLeft,this,&ECEnemyChaser::onChasedEntityLeavesMap_);

    // listen to when the target entity enters/leaves stop distance
    entityControlled()->map()->game()->addWatchedEntity(entity,entityControlled(),stopDistance_);

    chaseStep_();
    chaseTimer_->start(2000); // TODO: store in a (modifiable) variable somewhere

    double distBW = distance(entityControlled()->pos(),entity->pos());
    emit entityChaseStarted(entity, distBW);
}

/// Executed whenever an entity leaves the fov of the controlled entity.
/// Will unset the leaving entity as the target entity.
void ECEnemyChaser::onEntityLeavesFOV_(Entity *entity)
{
    qDebug() << "onEntityLeavesFOV_ executed"; // TODO: remove, test

    // if leaving entity isn't the target of controlled entity, do nothing
    if (entity != targetEntity_)
        return;

    // other wise

    // unset as target
    targetEntity_ = nullptr;

    // stop listening to enter/leave range for leaving entity
    entityControlled()->map()->game()->removeWatchedEntity(entity,entityControlled());
    chaseTimer_->stop();

    // if there is another enemy in view, target that one
    std::unordered_set<Entity*> otherEntitiesInView = fovEmitter_->entitiesInView();
    for (Entity* possibleEnemy:otherEntitiesInView){
        if (targetEntity_->isAnEnemyGroup(possibleEnemy->group())){
            onEntityEntersFOV_(possibleEnemy);
            return;
        }
    }
}

/// Executed whenever the controlled entity moves towards its chase target.
void ECEnemyChaser::onEntityMoved_()
{
    // do nothing if nothing being chased
    if (targetEntity_.isNull())
        return;

    double distBW = distance(entityControlled()->pos(),targetEntity_->pos());
    emit entityChaseContinued(targetEntity_,distBW);
}

/// Executed whenever the controlled entity has just reached the stop distance from the chased entity.
/// Will stop moving towards the chased entity.
void ECEnemyChaser::onEntityEntersRange_(Entity *watched, Entity *watching, double range)
{
    qDebug() << "onEntityEntersRange_ executed"; // TODO: remove test
    pathMover_->stopMovingEntity();
    paused_ = true;
}

/// Executed whenever the chased entity just leaves stop distance from controlled entity.
/// Will start chasing it again.
void ECEnemyChaser::onEntityLeavesRange_(Entity *watched, Entity *watching, double range)
{
    qDebug() << "onEntityLeavesRange_ executed"; // TODO: remove test
    paused_ = false;
}

/// Executed when the chasing entity dies.
/// Will stop chasing.
void ECEnemyChaser::onChasingEntityDies_(QObject *entity)
{
    stopChasing();
}

/// Executed when the chased entity dies.
/// Will stop chasing.
void ECEnemyChaser::onChasedEntityDies_(QObject *entity)
{
    stopChasing();
}

/// Executed when the controlled entity (chasing entity) leaves a map.
/// If it left to no map (i.e. map ptr is now nullptr), will stop chasing.
void ECEnemyChaser::onChasingEntityLeavesMap_(Entity *entity)
{
    if (entity->map() == nullptr)
        stopChasing();
}

/// Executed when the chased entity leaves a Map.
/// If it left to no map (i.e. map ptr is now nullptr), will stop chasing.
void ECEnemyChaser::onChasedEntityLeavesMap_(Entity *entity)
{
    if (entity->map() == nullptr)
        stopChasing();
}

/// Takes controlled entity one step closer to chase victim :P (if chasing something).
void ECEnemyChaser::chaseStep_()
{
    // if whats being chased has died, stop chasing
    // if the thing chasing has died, stop chasing
    if (targetEntity_.isNull() || entityControlled() == nullptr){
        stopChasing();
        return;
    }

    // make sure entity and one being chased are in a map
    Map* entitysMap = entityControlled()->map();
    Map* chaseVictimsMap = targetEntity_->map();
    assert(entitysMap != nullptr && chaseVictimsMap != nullptr);

    // make sure is supposed to be chasing right now
    assert(shouldChase_);

    // order to move towards chase victim :P
    if (!paused_)
        pathMover_->moveEntity(targetEntity_->pos());
}
